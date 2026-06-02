#pragma once
#include <SFML/Network.hpp>
#include <SFML/System.hpp>
#include <iostream>
#include <optional>
#include <map>
#include <vector>
#include "NetworkProtocol.h" 

// --- LIBRERÍAS DE LA THREAD POOL ---
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class Server {
private:
    struct MatchData {
        std::optional<sf::IpAddress> clientIPs[2];
        unsigned short clientPorts[2];
        bool clientConnected[2];

        unsigned int lastTimestamps[2];
        short playerInputs[2];

        std::vector<unsigned short> processedCriticalPackets[2];

        // Último input normal procesado de cada jugador para reconciliar en cliente
        unsigned short lastProcessedMovementID[2];

        S2C_StatePacket worldState;
        sf::Clock serverClock;
        sf::Clock endClock;
        bool pendingDelete;
    };

    unsigned short port;
    bool isRunning;
    sf::UdpSocket udpSocket;

    std::map<unsigned short, MatchData> matches;

    // --- VARIABLES DE LA THREAD POOL ---
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::mutex dataMutex; // ¡NUEVO! Protege el mapa de partidas 'matches'
    std::condition_variable condition;
    bool stopPool;

    // --- MÉTODOS DE LA THREAD POOL ---
    void ThreadPoolWorker();
    void EnqueueTask(std::function<void()> task);

    // Métodos divididos correctamente
    void ProcessPackets();
    void UpdatePhysics(MatchData& match);
    void BroadcastState(MatchData& match);
    void InitMatch(MatchData& match, unsigned short matchID);
    void CleanupMatches();

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();

    void CheckCollisions(MatchData& match);
    void HandlePlayerDeath(MatchData& match, short playerIndex, short killerID);
};