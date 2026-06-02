#pragma once
#include <SFML/Network.hpp>
#include <SFML/System.hpp>
#include <iostream>
#include <optional>
#include <map>
#include <vector>
#include "NetworkProtocol.h" 


class Server {
private:
    struct MatchData {
        // Datos de la sala 1v1 (Arrays clásicos en lugar de vectores para optimizar RAM)
        std::optional<sf::IpAddress> clientIPs[2]; // <--- CAMBIA ESTO
        unsigned short clientPorts[2];
        bool clientConnected[2];

        unsigned int lastTimestamps[2];
        short playerInputs[2];

        std::vector<unsigned int> processedCriticalPackets[2];

        // Último input normal procesado de cada jugador para reconciliar en cliente
        unsigned int lastProcessedMovementID[2];

        S2C_StatePacket worldState;
        sf::Clock serverClock;
        sf::Clock endClock;
        bool pendingDelete;
    };

    unsigned short port;
    bool isRunning;
    sf::UdpSocket udpSocket;

    std::map<unsigned int, MatchData> matches;

    // Métodos divididos correctamente
    void ProcessPackets();
    void UpdatePhysics(MatchData& match);
    void BroadcastState(MatchData& match);
    void InitMatch(MatchData& match, unsigned int matchID);
    void CleanupMatches();

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();


    void CheckCollisions(MatchData& match);

    void HandlePlayerDeath(MatchData& match, short playerIndex, short killerID);
};