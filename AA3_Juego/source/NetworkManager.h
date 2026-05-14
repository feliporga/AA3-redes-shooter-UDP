#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <optional> 
#include "NetworkProtocol.h"

#define NM NetworkManager::Instance()

#define SERVER_IP "127.0.0.1"
#define SERVER_TCP_PORT 60000
#define SERVER_UDP_PORT 60001

// Estructura para guardar el ranking cuando llegue
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

    // SFML 3 exige optional para las IPs
    std::optional<sf::IpAddress> serverUdpIp;

    NetworkManager() {
        tcpSocket.setBlocking(false);
        udpSocket.setBlocking(false);
        serverUdpIp = sf::IpAddress::resolve(SERVER_IP);
    }

public:
    static NetworkManager& Instance() {
        static NetworkManager instance;
        return instance;
    }

    short myPlayerID = 0;
    bool isConnected = false;

    // Variables para el Ranking
    bool newRankingAvailable = false;
    std::vector<PlayerRanking> lastRanking;

    // Variables para el Login
    bool loginSuccess = false;
    bool loginError = false;
    bool gameStarted = false;

    // CONEXIÓN TCP
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

    // ENVÍO DE PAQUETES TCP
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

    // BUCLE DE ESCUCHA TCP
    void Listen() {
        sf::Packet packet;
        if (tcpSocket.receive(packet) == sf::Socket::Status::Done) {
            short typeInt;
            if (packet >> typeInt) {
                PacketType type = static_cast<PacketType>(typeInt);

                if (type == PacketType::RankingResponse) {
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
                    std::string udpIP;
                    unsigned short udpPort;

                    // Extraemos los datos exactos enviamos desde TCP
                    packet >> pID >> udpIP >> udpPort;

					myPlayerID = pID; // Guardamos si somos Mario o Luigi (1 o 2)
                    gameStarted = true;

                    std::cout << "[CLIENTE] PARTIDA ENCONTRADA! Soy el jugador " << myPlayerID << std::endl;
                }
            }
        }
    }

    // ENVÍO Y RECEPCIÓN UDP (GAMEPLAY)
    void SendUDPInput(short mask, unsigned int time) {
        if (!serverUdpIp.has_value()) return;

        C2S_InputPacket packet;
        packet.playerID = myPlayerID;
        packet.timestamp = time;
        packet.inputMask = mask;

        udpSocket.send(&packet, sizeof(C2S_InputPacket), serverUdpIp.value(), SERVER_UDP_PORT);
    }

    bool ReceiveUDPState(S2C_StatePacket& outState) {
        char buffer[1024];
        std::size_t received;
        std::optional<sf::IpAddress> sender;
        unsigned short port;

        if (udpSocket.receive(buffer, sizeof(buffer), received, sender, port) == sf::Socket::Status::Done) {
            if (received == sizeof(S2C_StatePacket)) {
                std::memcpy(&outState, buffer, sizeof(S2C_StatePacket));
                return true;
            }
        }
        return false;
    }
};