#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <string>
#include "NetworkProtocol.h"
#include "SceneManager.h"
#include "TicTacToe.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 55000


#define NM NetworkManager::Instance()

#define NUM_ROWS 6
#define NUM_COLS 6
#define INITIAL_TURN 1

class NetworkManager {
private:
    sf::TcpSocket socket;
    sf::TcpListener p2pListener;
    std::vector<sf::TcpSocket*> p2pPeers;

    std::string savedUser = "";
    std::string savedPass = "";

    int incomingConnections = 0;
    bool isConnected = false;

    NetworkManager() {
        socket.setBlocking(false);
    }

public:
    bool nextGameMyTurn = false;
    std::string nextGameOpponent = "";
    int nextGamePlayerID = INITIAL_TURN;
    static NetworkManager& Instance() {
        static NetworkManager instance;
        return instance;
    }
    

    // Estructura para guardar los datos del ranking temporalmente
    struct PlayerRecord {
        int pos;
        std::string name;
        int pts;
        int v;
        int d;
    };
    std::map<int, PlayerRecord> playersInMatch;
    std::vector<PlayerRecord> lastRanking;
    bool newRankingAvailable = false;

    bool ConnectToServer(const std::string& ip, unsigned short port) {
        socket.setBlocking(true);

        sf::Socket::Status status = socket.connect(sf::IpAddress::resolve(ip).value(), port, sf::seconds(2.0f));

        socket.setBlocking(false);

        if (status == sf::Socket::Status::Done) {
            std::cout << "[CLIENTE] Conectado al servidor con exito!" << std::endl;
            isConnected = true;
            return true;
        }
        std::cout << "[CLIENTE] Error al conectar con el servidor." << std::endl;
        return false;
    }

    void SendLogin(const std::string& username, const std::string& password) {
        if (!isConnected) {
            std::cout << "[CLIENTE] No estas conectado al servidor." << std::endl;
            return;
        }

        // GUARDAMOS LAS CREDENCIALES PARA EL AUTO-LOGIN
        savedUser = username;
        savedPass = password;

        sf::Packet packet;
        packet << static_cast<int>(PacketType::LoginRequest) << username << password;

        if (socket.send(packet) == sf::Socket::Status::Done) {
            std::cout << "[CLIENTE] Peticion de Login enviada al servidor." << std::endl;
        }
    }

    void Listen() {
        if (!isConnected) return;
        //Aceptar conexiones de los otros jugadores para P2P
        if (incomingConnections > 0) {
            sf::TcpSocket* incomingPeer = new sf::TcpSocket();
            if (p2pListener.accept(*incomingPeer) == sf::Socket::Status::Done) {
                incomingPeer->setBlocking(false);
                p2pPeers.push_back(incomingPeer);
                incomingConnections--;
                std::cout << "[P2P] Un jugador se ha conectado a mi puerto, faltan: " << incomingConnections << " por entrar." << std::endl;
            }
            else {
                delete incomingPeer;
            }
        }

        for (auto* peer : p2pPeers) {
            sf::Packet peerPacket;
            while (peer->receive(peerPacket) == sf::Socket::Status::Done) {
                int typeInt;
                if (peerPacket >> typeInt) {
                    if (typeInt == static_cast<int>(PacketType::UpdateBoard)) {
                        int r, c, playerID, nextPlayerTurn;
                        peerPacket >> r >> c >> playerID >> nextPlayerTurn;

                        //LINEA DE DEBUG POR IA PARA DETECTAR ERROR
                        std::cout << "[P2P] He recibido jugada del Jugador " << playerID << "! Pasa el turno al Jugador " << nextPlayerTurn << std::endl;
                        TicTacToe* gameScene = dynamic_cast<TicTacToe*>(SceneManager::Instance().GetCurrentScene());
                        if (gameScene) {
                            gameScene->ApplyMoveFromServer(r, c, playerID, nextPlayerTurn);
                        }
                    }
                }
            }
        }
        sf::Packet packet;
       
        if (socket.receive(packet) == sf::Socket::Status::Done) {
            int typeInt;
            packet >> typeInt;
            PacketType type = static_cast<PacketType>(typeInt);

            switch (type) {
            case PacketType::LoginSuccess:
                std::cout << "[CLIENTE] El servidor acepto el Login! Entrando al juego..." << std::endl;
                SceneManager::Instance().SetNextScene("MainMenu");
                break;
            case PacketType::LoginFailed:
                std::cout << "[CLIENTE] Error: Usuario o contrasena incorrectos." << std::endl;
                break;
            case PacketType::RegisterSuccess:
                std::cout << "[CLIENTE] Cuenta creada con exito! Ahora puedes hacer Login." << std::endl;
                break;
            case PacketType::RegisterFailed:
                std::cout << "[CLIENTE] Error: El nombre de usuario ya existe." << std::endl;
                break;

            case PacketType::RankingResponse:
                {
                int numPlayers;
                packet >> numPlayers;
                lastRanking.clear(); 

                for (int i = 0; i < numPlayers; i++) {
                    PlayerRecord rec;
                    packet >> rec.pos >> rec.name >> rec.pts >> rec.v >> rec.d;
                    lastRanking.push_back(rec);
                }
                newRankingAvailable = true; 
                break;
                }
            case PacketType::RoomSuccess:
                std::cout << "[CLIENTE] Operacion de sala correcta. Esperando jugadores..." << std::endl;
                break;
            case PacketType::RoomError:
                std::cout << "[CLIENTE] Error: La sala ya existe, esta llena o no se encontro." << std::endl;
                break;


            case PacketType::GameStart:
            {
                int myID;
                unsigned short myPort;
                int numPeers;

                packet >> myID >> myPort >> numPeers;


                this->nextGameMyTurn = (myID == 1); 
                this->nextGameOpponent = "Jugadores P2P";
                this->nextGamePlayerID = myID;


                socket.disconnect();
                std::cout << "\n[CLIENTE] Bootstrap completado. Mi ID P2P es: " << myID << std::endl;

                incomingConnections = 4 - myID;

                if (incomingConnections > 0) {
                    p2pListener.setBlocking(false);
                    p2pListener.listen(myPort);
                    std::cout << "[P2P] Escuchando en el puerto " << myPort << " a " << incomingConnections << " companeros..." << std::endl;
                }

                for (int i = 0; i < numPeers; i++) {
                    int peerID;
                    std::string peerIP;
                    unsigned short peerPort;
                    std::string peerName;
                    int peerScore;

                    packet >> peerID >> peerIP >> peerPort >> peerName >> peerScore;

                    PlayerRecord record;
                    record.name = peerName;
                    record.pts = peerScore;

                    playersInMatch[peerID] = record;
                    if (myID > peerID) {
                        sf::TcpSocket* newPeer = new sf::TcpSocket();
                        newPeer->setBlocking(true); // Bloqueamos solo un momento para conectar

                        sf::Socket::Status status = newPeer->connect(sf::IpAddress::resolve(peerIP).value(), peerPort, sf::seconds(5.0f));
                        newPeer->setBlocking(false);

                        if (status == sf::Socket::Status::Done) {
                            p2pPeers.push_back(newPeer);
                            std::cout << "[P2P] Conectado al Jugador " << peerID << " con exito!" << std::endl;
                        }
                        else {
                            std::cout << "[P2P ERROR] Fallo al conectar con el Jugador " << peerID << std::endl;
                            delete newPeer;
                        }
                    }
                }
                isConnected = true;
                SceneManager::Instance().SetNextScene("TicTacToe");
                break;
            }

            }

        }
    }

    void SendRegister(const std::string& username, const std::string& password) {
        if (!isConnected) {
            std::cout << "[CLIENTE] No estas conectado al servidor." << std::endl;
            return;
        }

        sf::Packet packet;
        packet << static_cast<int>(PacketType::RegisterRequest) << username << password;

        if (socket.send(packet) == sf::Socket::Status::Done) {
            std::cout << "[CLIENTE] Peticion de Registro enviada." << std::endl;
        }
    }

    void RequestRanking() {
        if (!isConnected) return;
        sf::Packet packet;
        packet << static_cast<int>(PacketType::RankingRequest);
        (void)socket.send(packet);
    }

    void SendCreateRoom(const std::string& roomName) {
        if (!isConnected) return;
        sf::Packet packet;
        packet << static_cast<int>(PacketType::CreateRoomRequest) << roomName;

        if (socket.send(packet) == sf::Socket::Status::Done) {
            std::cout << "[CLIENTE] Peticion para CREAR sala '" << roomName << "' enviada." << std::endl;
        }
    }

    void SendJoinRoom(const std::string& roomName) {
        if (!isConnected) return;
        sf::Packet packet;
        packet << static_cast<int>(PacketType::JoinRoomRequest) << roomName;

        if (socket.send(packet) == sf::Socket::Status::Done) {
            std::cout << "[CLIENTE] Peticion para UNIRSE a sala '" << roomName << "' enviada." << std::endl;
        }
    }

   
    void SendGameMove(int row, int col, int ID, int nextTurn) {
        if (!isConnected) return;
        std::cout << "[CLIENTE] Pieza clickada en fila " << row << "y columna " << col << std::endl;

        // ahora deja espectear y se salta el turno de quien ha ganado

        sf::Packet packet;
        packet << static_cast<int>(PacketType::UpdateBoard) << static_cast<int>(row) << static_cast<int>(col)
            << static_cast<int>(ID) << static_cast<int>(nextTurn);

        // Enviamos el tablero actualizado a los otros jugadores a la vez
        for (auto* peer : p2pPeers) {
            sf::Socket::Status status = peer->send(packet);
            if (status != sf::Socket::Status::Done) {
                std::cout << "[P2P ERROR] Fallo al enviar paquete a la tuberia." << std::endl;
            }
        }
    }


    void SendMatchResult(const std::string& roomName, int first, int second, int third, int fourth) {
        
        socket.disconnect();
        socket.setBlocking(true); 

        
        if (socket.connect(sf::IpAddress::resolve(SERVER_IP).value(), 55000) == sf::Socket::Status::Done) {
            isConnected = true;

            // enviamos el resultado num�rico // comentarios debug con ia para ayuda
            sf::Packet packet;
            packet << static_cast<int>(PacketType::ReportResult) << roomName << first << second << third << fourth;

            if (socket.send(packet) == sf::Socket::Status::Done) {
                std::cout << "[CLIENTE] Resultado enviado (IDs: " << first << ", " << second << ", " << third << ", " << fourth << ")." << std::endl;
            }
        }
        else {
            std::cout << "[CLIENTE ERROR] No se pudo reconectar al servidor." << std::endl;
        }

        socket.setBlocking(false); 
    }


    void LeaveMatchAndReconnect() {
        ResetP2P();

        // Nos reconectamos al servidor
        socket.disconnect();
        socket.setBlocking(true);

        if (socket.connect(sf::IpAddress::resolve(SERVER_IP).value(), 55000) == sf::Socket::Status::Done) {
            isConnected = true;
            std::cout << "[CLIENTE] Reconectado al servidor principal. Autenticando..." << std::endl;

            // Enviamos el Auto-Login
            sf::Packet packet;
            packet << static_cast<int>(PacketType::LoginRequest) << savedUser << savedPass;
            socket.send(packet);
        }
        else {
            std::cout << "[CLIENTE ERROR] No se pudo reconectar al servidor principal." << std::endl;
        }

        socket.setBlocking(false);
    }


    // resetear partida anterior
    void ResetP2P() {
        for (auto* peer : p2pPeers) {
            peer->disconnect();
            delete peer;
        }
        p2pPeers.clear();
        p2pListener.close();
        incomingConnections = 0;

        // Reseteamos las variables de partida
        nextGameMyTurn = false;
        nextGameOpponent = "";
        nextGamePlayerID = INITIAL_TURN;

        std::cout << "[CLIENTE] Conexiones P2P cerradas. Listo para nueva partida." << std::endl;
    }
};