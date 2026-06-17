#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <optional> 
#include <cstring>
#include <fstream> // Launcher
#include "NetworkProtocol.h"


#define SERVER_UDP_IP "127.0.0.1"
#define SERVER_TCP_PORT 55009
#define SERVER_UDP_PORT 55010
#define SERVER_TCP_IP "127.0.0.1"
#define NM NetworkManager::Instance()


struct PlayerRanking {
    short pos;
    std::string name;
    short pts;
    short v;
    short d;
};



class NetworkManager {
private:
    sf::TcpSocket tcpSocket;
    sf::UdpSocket udpSocket;

    std::string savedUser = "";
    std::string savedPass = "";
    std::optional<sf::IpAddress> serverUdpIp;

    struct PendingPacket {
        C2S_InputPacket packet;
        unsigned int lastSendTime;
    };
    std::vector<PendingPacket> unackedPackets;
    unsigned short nextPacketID = 1;

    NetworkManager() {
        tcpSocket.setBlocking(false);
        udpSocket.setBlocking(false);
        serverUdpIp = sf::IpAddress::resolve(SERVER_UDP_IP);
    }

    void ClearTransientUpdateState() {
        // Estos campos son eventos temporales.
        // Si no se limpian al empezar un tick nuevo, el cliente puede aplicar un BACK antiguo para siempre.
        lastState.correctionRequired = false;
        lastState.irregularPlayerID = 0;
        lastState.irregularityCount = 0;

        lastState.movementBackRequired = false;
        lastState.backPlayerID = 0;
        lastState.backMovementID = 0;
        lastState.backPosX = 0;
        lastState.backPosY = 0;
    }

    bool PrepareIncomingUpdate(unsigned int timestamp, unsigned short matchID) {
        if (matchID != currentMatchID) return false;

        // Si llega un update antiguo por UDP, no lo usamos para reconstruir estado.
        if (lastState.timestamp != 0 && timestamp < lastState.timestamp) {
            return false;
        }

        // Tick nuevo: reseteamos lo recibido para no mezclar jugadores/balas/correcciones antiguas.
        if (timestamp != lastState.timestamp) {
            lastState.matchID = matchID;
            lastState.timestamp = timestamp;

            receivedPlayerUpdate[0] = false;
            receivedPlayerUpdate[1] = false;
            receivedBulletUpdate = false;
            receivedCorrectionUpdate = false;
            baseStateQueued = false;

            ClearTransientUpdateState();
        }

        return true;
    }

    void TryQueueBaseState() {
        if (receivedPlayerUpdate[0] &&
            receivedPlayerUpdate[1] &&
            receivedBulletUpdate &&
            receivedCorrectionUpdate &&
            !baseStateQueued) {
            pendingStates.push_back(lastState);
            newStateReceived = true;
            baseStateQueued = true;
        }
    }

public:
    std::string GetSavedUser() const { return savedUser; }

    static NetworkManager& Instance() {
        static NetworkManager instance;
        return instance;
    }

    short myPlayerID = 0;
    unsigned short currentMatchID = 0;
    bool isConnected = false;
    bool connectionWarning = false;

    bool newRankingAvailable = false;
    std::vector<PlayerRanking> lastRanking;
    bool loginSuccess = false;
    bool loginError = false;
    bool gameStarted = false;
    // Esta variable indica si el resultado ya se ha enviado
    bool resultReported = false;

    ClientWorldState lastState;
    bool newStateReceived = false;

    // Estados locales reconstruidos a partir de updates separados del servidor.
    // No son paquetes de red ni estados completos enviados por el servidor.
    std::vector<ClientWorldState> pendingStates;

    // Como ahora el servidor envia updates separados, esperamos a tener jugadores, balas
    // y correccion del mismo tick antes de entregar un estado sintetizado a GameScene.
    bool receivedPlayerUpdate[2] = { false, false };
    bool receivedBulletUpdate = false;
    bool receivedCorrectionUpdate = false;
    bool baseStateQueued = false;

    unsigned short dynamicUdpPort = SERVER_UDP_PORT;
    // TCP

    bool ConnectToServer(const std::string& ip, unsigned short port) {
        tcpSocket.setBlocking(true);
        sf::Socket::Status status = tcpSocket.connect(sf::IpAddress::resolve(ip).value(), port, sf::seconds(2.0f));
        tcpSocket.setBlocking(false);

        if (status == sf::Socket::Status::Done) {
            std::cout << "[CLIENTE] Conectado al servidor TCP!" << std::endl;
            isConnected = true;
            return true;
        }
        return false;
    }

    void SendCheckMapVersion() {
        short version = 0; // Por defecto asumimos que no hay versión
        std::ifstream versionFile("resources/mapa_version.txt");
        if (versionFile.is_open()) {
            versionFile >> version;
            versionFile.close();
        }

        sf::Packet p;
        p << (short)PacketType::CheckMapVersion << version;
        tcpSocket.send(p);
    }

    void SendLogin(const std::string& user, const std::string& pass) {
        savedUser = user; savedPass = pass;
        sf::Packet p;
        p << (short)PacketType::LoginRequest << user << pass;
        tcpSocket.send(p);
    }

    void SendRegister(const std::string& user, const std::string& pass) {
        sf::Packet p;
        p << (short)PacketType::RegisterRequest << user << pass;
        tcpSocket.send(p);
    }

    void RequestRanking() {
        sf::Packet p;
        p << (short)PacketType::RankingRequest;
        tcpSocket.send(p);
    }

    void Listen() {
        sf::Packet packet;
        if (tcpSocket.receive(packet) == sf::Socket::Status::Done) {
            short typeInt;
            if (packet >> typeInt) {
                PacketType type = static_cast<PacketType>(typeInt);

                if (type == PacketType::UpdateMap) {
                    short newVersion;
                    std::string newMapData;
                    packet >> newVersion >> newMapData;

                    // Escribimos el nuevo archivo mapa.txt
                    std::ofstream mapFile("resources/mapa.txt");
                    if (mapFile.is_open()) {
                        mapFile << newMapData;
                        mapFile.close();
                    }

                    // Actualizamos el txt de la versión local
                    std::ofstream versionFile("resources/mapa_version.txt");
                    if (versionFile.is_open()) {
                        versionFile << newVersion;
                        versionFile.close();
                    }
                    std::cout << "[LAUNCHER] Archivos actualizados a la version " << newVersion << " con exito." << std::endl;
                }
                else if (type == PacketType::RankingResponse) {
                    short size;
                    if (packet >> size) {
                        lastRanking.clear();
                        for (short i = 0; i < size; i++) {
                            PlayerRanking rank;
                            packet >> rank.pos >> rank.name >> rank.pts >> rank.v >> rank.d;
                            lastRanking.push_back(rank);
                        }
                        newRankingAvailable = true;
                    }
                }
                else if (type == PacketType::LoginSuccess) {
                    loginSuccess = true;
                }
                else if (type == PacketType::LoginFailed) {
                    loginError = true;
                }
                else if (type == PacketType::RegisterSuccess) {
                    std::cout << "[CLIENTE] Registro completado. Ya puedes loguearte." << std::endl;
                }
                else if (type == PacketType::GameStart) {
                    short pID;
                    unsigned short matchID;
                    std::string udpIP;
                    unsigned short udpPort;

                    packet >> pID >> matchID >> udpIP >> udpPort;

                    myPlayerID = pID;
                    currentMatchID = matchID;
                    dynamicUdpPort = udpPort;

                    // Usamos la IP UDP que envia el servidor TCP para llegar al servidor dedicado de gameplay
                    serverUdpIp = sf::IpAddress::resolve(udpIP);

                    lastState = ClientWorldState{};
                    newStateReceived = false;

                    // Al empezar una partida nueva no queremos procesar estados reconstruidos de la partida anterior.
                    pendingStates.clear();
                    receivedPlayerUpdate[0] = false;
                    receivedPlayerUpdate[1] = false;
                    receivedBulletUpdate = false;
                    receivedCorrectionUpdate = false;
                    baseStateQueued = false;
                    unackedPackets.clear();
                    nextPacketID = 1;

                    // Al empezar una nueva partida el resultado queda pendiente de enviar
                    resultReported = false;
                    gameStarted = true;
                    std::cout << "[CLIENTE] PARTIDA ENCONTRADA! Soy el jugador " << myPlayerID << " en el puerto " << dynamicUdpPort << std::endl;
                }
            }
        }
    }
    // Esta funcion envia el resultado al servidor TCP
    void SendReportResult(bool hasWon) {
        sf::Packet p;
        p << (short)PacketType::ReportResult << hasWon;
        tcpSocket.send(p);
    }

    // UDP

    void SendUDPInput(short mask, unsigned short movementID, bool hasPredictedPosition, short predictedPosX, short predictedPosY) {
        if (!serverUdpIp.has_value()) return;

        C2S_InputPacket packet;
        packet.flags = NORMAL_PACKET;
        packet.packetID = 0;
        packet.playerID = myPlayerID;
        packet.inputMask = mask;
        packet.matchID = currentMatchID;

        // Los inputs normales llevan ID para que el cliente pueda reconciliar
        packet.movementID = movementID;

        // El cliente envia la posicion que ha predicho para que el servidor la valide
        packet.hasPredictedPosition = hasPredictedPosition;
        packet.predictedPosX = predictedPosX;
        packet.predictedPosY = predictedPosY;

        udpSocket.send(&packet, sizeof(C2S_InputPacket), serverUdpIp.value(), dynamicUdpPort);
    }

    void SendCriticalUDPInput(short mask, unsigned int time) {
        if (!serverUdpIp.has_value()) return;

        C2S_InputPacket packet;

        // El disparo es a la vez CRITICO y URGENTE:
        // CRITICAL_PACKET: requiere ACK y reenvio si se pierde.
        // URGENT_PACKET: el servidor lo procesa con prioridad sobre los movimientos normales.
        packet.flags = CRITICAL_PACKET | URGENT_PACKET;
        packet.packetID = nextPacketID++;
        packet.playerID = myPlayerID;
        packet.inputMask = mask;
        packet.matchID = currentMatchID;

        // Los disparos usan packetID para ACK, no forman parte de la reconciliación de movimiento
        packet.movementID = 0;

        // Los paquetes criticos no validan posicion, solo la accion concreta
        packet.hasPredictedPosition = false;
        packet.predictedPosX = 0;
        packet.predictedPosY = 0;

        udpSocket.send(&packet, sizeof(C2S_InputPacket), serverUdpIp.value(), dynamicUdpPort);
        unackedPackets.push_back({ packet, time });
    }

    void SendUDPPong(unsigned short pingID, unsigned int pingTimestamp) {
        if (!serverUdpIp.has_value()) return;
        if (myPlayerID == 0 || currentMatchID == 0) return;

        C2S_PongPacket pong;
        pong.flags = NORMAL_PACKET;
        pong.matchID = currentMatchID;
        pong.playerID = myPlayerID;
        pong.pingID = pingID;

        // Se devuelve exactamente el timestamp del Ping.
        // El servidor usara este dato para calcular RTT.
        pong.timestamp = pingTimestamp;

        udpSocket.send(&pong, sizeof(C2S_PongPacket), serverUdpIp.value(), dynamicUdpPort);
        std::cout << "[CLIENTE] PONG enviado al servidor UDP. PingID: " << pingID << std::endl;
    }

    void ListenUDP() {
        char buffer[1024];
        std::size_t received;
        std::optional<sf::IpAddress> sender;
        unsigned short port;

        while (udpSocket.receive(buffer, sizeof(buffer), received, sender, port) == sf::Socket::Status::Done) {
            if (received < sizeof(uint8_t) + sizeof(short)) continue;

            short type;
            std::memcpy(&type, buffer + 1, sizeof(short)); // Saltamos el byte de flags

            if (type == (short)PacketType::Ping) {
                if (received < sizeof(S2C_PingPacket)) continue;

                S2C_PingPacket ping;
                std::memcpy(&ping, buffer, sizeof(S2C_PingPacket));

                if (ping.matchID != currentMatchID) continue;
                if (ping.playerID != 0 && ping.playerID != myPlayerID) continue;

                // PING/PONG:
                // El servidor pregunta si seguimos vivos y el cliente responde inmediatamente con PONG.
                // El PONG conserva el timestamp original para que el servidor calcule RTT.
                SendUDPPong(ping.pingID, ping.timestamp);
            }
            else if (type == (short)PacketType::PlayerPositionUpdate) {
                connectionWarning = false;
                if (received < sizeof(S2C_PlayerPositionUpdatePacket)) continue;

                S2C_PlayerPositionUpdatePacket playerUpdate;
                std::memcpy(&playerUpdate, buffer, sizeof(S2C_PlayerPositionUpdatePacket));

                if (!PrepareIncomingUpdate(playerUpdate.timestamp, playerUpdate.matchID)) continue;

                short index = playerUpdate.player.id - 1;
                if (index < 0 || index >= 2) continue;

                // El servidor envia updates de posicion, no un estado completo.
                // El cliente reconstruye su ultimo estado localmente para que GameScene pueda seguir trabajando igual.
                lastState.players[index] = playerUpdate.player;
                receivedPlayerUpdate[index] = true;

                TryQueueBaseState();
            }
            else if (type == (short)PacketType::BulletUpdate) {
                connectionWarning = false;
                if (received < sizeof(S2C_BulletUpdatePacket)) continue;

                S2C_BulletUpdatePacket bulletUpdate;
                std::memcpy(&bulletUpdate, buffer, sizeof(S2C_BulletUpdatePacket));

                if (!PrepareIncomingUpdate(bulletUpdate.timestamp, bulletUpdate.matchID)) continue;

                for (short i = 0; i < MAX_BULLETS; i++) {
                    lastState.bullets[i] = bulletUpdate.bullets[i];
                }

                receivedBulletUpdate = true;

                // Entregamos el estado base solo cuando ya tenemos jugadores, balas y correccion del mismo tick.
                TryQueueBaseState();
            }
            else if (type == (short)PacketType::CorrectionUpdate) {
                connectionWarning = false;
                if (received < sizeof(S2C_CorrectionUpdatePacket)) continue;

                S2C_CorrectionUpdatePacket correctionUpdate;
                std::memcpy(&correctionUpdate, buffer, sizeof(S2C_CorrectionUpdatePacket));

                if (!PrepareIncomingUpdate(correctionUpdate.timestamp, correctionUpdate.matchID)) continue;

                lastState.correctionRequired = correctionUpdate.correctionRequired;
                lastState.irregularPlayerID = correctionUpdate.irregularPlayerID;
                lastState.irregularityCount = correctionUpdate.irregularityCount;
                lastState.irregularityEventID = correctionUpdate.irregularityEventID;

                lastState.movementBackRequired = correctionUpdate.movementBackRequired;
                lastState.backPlayerID = correctionUpdate.backPlayerID;
                lastState.backMovementID = correctionUpdate.backMovementID;
                lastState.backPosX = correctionUpdate.backPosX;
                lastState.backPosY = correctionUpdate.backPosY;

                receivedCorrectionUpdate = true;

                // Ahora el estado se entrega una sola vez por tick,
                // con posiciones + balas + correccion ya coherentes.
                TryQueueBaseState();
            }
            else if (type == (short)PacketType::GameOverUpdate) {
                connectionWarning = false;
                if (received < sizeof(S2C_GameOverUpdatePacket)) continue;

                S2C_GameOverUpdatePacket gameOverUpdate;
                std::memcpy(&gameOverUpdate, buffer, sizeof(S2C_GameOverUpdatePacket));

                if (!PrepareIncomingUpdate(gameOverUpdate.timestamp, gameOverUpdate.matchID)) continue;

                lastState.isGameOver = gameOverUpdate.isGameOver;
                lastState.winnerID = gameOverUpdate.winnerID;
                lastState.gameOverByIrregularities = gameOverUpdate.gameOverByIrregularities;
                lastState.irregularPlayerID = gameOverUpdate.irregularPlayerID;

                pendingStates.push_back(lastState);
                newStateReceived = true;
            }
            else if (type == (short)PacketType::Ack) {
                S2C_AckPacket ack;
                std::memcpy(&ack, buffer, sizeof(S2C_AckPacket));

                for (size_t i = 0; i < unackedPackets.size(); ) {
                    if (unackedPackets[i].packet.packetID == ack.ackedPacketID) {
                        std::cout << "[CLIENTE] ACK recibido (ID: " << ack.ackedPacketID << "). Borrando de la cola." << std::endl;
                        // Borramos usando el iterador begin() más el índice actual
                        unackedPackets.erase(unackedPackets.begin() + i);
                    }
                    else {
                        // Solo avanzamos el índice si NO hemos borrado nada
                        i++;
                    }
                }
            }
            else if (type == (short)PacketType::TimeoutWarning) {
                connectionWarning = true;
                std::cout << "[CLIENTE] ALERTA: El servidor va a desconectarnos por inactividad!" << std::endl;
            }
        }
    }

    void RetryUnackedPackets(unsigned int currentTime) {
        if (!serverUdpIp.has_value()) return;

        short numUnacked = (short)unackedPackets.size();
        for (short i = 0; i < numUnacked; i++) {
            if (currentTime - unackedPackets[i].lastSendTime > 100) {
                udpSocket.send(&unackedPackets[i].packet, sizeof(C2S_InputPacket), serverUdpIp.value(), dynamicUdpPort);
                unackedPackets[i].lastSendTime = currentTime;
                std::cout << "[CLIENTE] Reenviando paquete critico/urgente perdido (ID: " << unackedPackets[i].packet.packetID << ")" << std::endl;
            }
        }
    }
    //Matchmaking

    void SendSearchCasual() {
        sf::Packet p;
        p << (short)PacketType::SearchCasualRequest;
        tcpSocket.send(p);
    }

    void SendSearchCompetitive(short myElo) {
        sf::Packet p;
        p << (short)PacketType::SearchCompetitiveRequest << myElo;
        tcpSocket.send(p);
    }

    void CancelSearch() {
        sf::Packet p;
        p << (short)PacketType::CancelSearchRequest;
        tcpSocket.send(p);
    }
};