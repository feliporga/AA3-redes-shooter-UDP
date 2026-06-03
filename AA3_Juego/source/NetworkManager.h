#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <optional> 
#include <cstring>
#include <fstream> // Launcher
#include "NetworkProtocol.h"

#ifndef NETWORK_MACROS
#define NETWORK_MACROS
#define SERVER_UDP_IP "77.230.37.161"
#define SERVER_TCP_PORT 60000
#define SERVER_UDP_PORT 60001
#define SERVER_TCP_IP "77.230.37.161"
#define NM NetworkManager::Instance()
#endif

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

public:
    std::string GetSavedUser() const { return savedUser; }

    static NetworkManager& Instance() {
        static NetworkManager instance;
        return instance;
    }

    short myPlayerID = 0;
    unsigned short currentMatchID = 0;
    bool isConnected = false;

    bool newRankingAvailable = false;
    std::vector<PlayerRanking> lastRanking;
    bool loginSuccess = false;
    bool loginError = false;
    bool gameStarted = false;
    // Esta variable indica si el resultado ya se ha enviado
    bool resultReported = false;

    S2C_StatePacket lastState;
    bool newStateReceived = false;

    unsigned short dynamicUdpPort = 60001;
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

    void SendCreateRoom(const std::string& roomName) {
        sf::Packet p;
        p << (short)PacketType::CreateRoomRequest << roomName;
        tcpSocket.send(p);
    }

    void SendJoinRoom(const std::string& roomName) {
        sf::Packet p;
        p << (short)PacketType::JoinRoomRequest << roomName;
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
                    //serverUdpIp = sf::IpAddress::resolve(udpIP);
                    serverUdpIp = tcpSocket.getRemoteAddress();

                    lastState = S2C_StatePacket{};
                    newStateReceived = false;
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
    void SendReportResult(short winnerPlayerID, bool hasWon) {
       /* sf::Packet p;
        p << (short)PacketType::ReportResult << winnerPlayerID;
        tcpSocket.send(p);*/
        sf::Packet p;
        p << (short)PacketType::ReportResult << hasWon;
        tcpSocket.send(p);
    }

    // UDP

    void SendUDPInput(short mask, unsigned int time, unsigned short movementID) {
        if (!serverUdpIp.has_value()) return;

        C2S_InputPacket packet;
        packet.flags = NORMAL_PACKET;
        packet.packetID = 0;
        packet.playerID = myPlayerID;
        packet.timestamp = time;
        packet.inputMask = mask;
        packet.matchID = currentMatchID;

        // Los inputs normales llevan ID para que el cliente pueda reconciliar
        packet.movementID = movementID;

        udpSocket.send(&packet, sizeof(C2S_InputPacket), serverUdpIp.value(), dynamicUdpPort);
    }

    void SendCriticalUDPInput(short mask, unsigned int time) {
        if (!serverUdpIp.has_value()) return;

        C2S_InputPacket packet;
        packet.flags = CRITICAL_PACKET;
        packet.packetID = nextPacketID++;
        packet.playerID = myPlayerID;
        packet.timestamp = time;
        packet.inputMask = mask;
        packet.matchID = currentMatchID;

        // Los disparos usan packetID para ACK, no forman parte de la reconciliación de movimiento
        packet.movementID = 0;

        udpSocket.send(&packet, sizeof(C2S_InputPacket), serverUdpIp.value(), dynamicUdpPort);
        unackedPackets.push_back({ packet, time });
    }

    void ListenUDP() {
        char buffer[1024];
        std::size_t received;
        std::optional<sf::IpAddress> sender;
        unsigned short port;

        while (udpSocket.receive(buffer, sizeof(buffer), received, sender, port) == sf::Socket::Status::Done) {
            short type;
            std::memcpy(&type, buffer + 1, sizeof(short)); // Saltamos el byte de flags

            if (type == (short)PacketType::UpdateBoard) {
                if (received < sizeof(S2C_StatePacket)) continue;

                S2C_StatePacket receivedState;
                std::memcpy(&receivedState, buffer, sizeof(S2C_StatePacket));

                if (receivedState.matchID != currentMatchID) continue;

                lastState = receivedState;
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
        }
    }

    void RetryUnackedPackets(unsigned int currentTime) {
        if (!serverUdpIp.has_value()) return;

        short numUnacked = (short)unackedPackets.size();
        for (short i = 0; i < numUnacked; i++) {
            if (currentTime - unackedPackets[i].lastSendTime > 100) {
                udpSocket.send(&unackedPackets[i].packet, sizeof(C2S_InputPacket), serverUdpIp.value(), dynamicUdpPort);
                unackedPackets[i].lastSendTime = currentTime;
                std::cout << "[CLIENTE] Reenviando paquete critico perdido (ID: " << unackedPackets[i].packet.packetID << ")" << std::endl;
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