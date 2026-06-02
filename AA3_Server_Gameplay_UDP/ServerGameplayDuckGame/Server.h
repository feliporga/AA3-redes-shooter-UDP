#pragma once
#include <SFML/Network.hpp>
#include <SFML/System.hpp>
#include <iostream>
#include <optional>
#include <map>
#include <vector>
#include "NetworkProtocol.h" 

// librerias para la pool de hilos
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

#define PLAYER_TIMEOUT_SECONDS 5
#define PLAYER_WARNING_SECONDS 2

class Server {
private:
    struct MatchData {
        // arrays de 2 para no usar ram extra
        std::optional<sf::IpAddress> clientIPs[2];
        unsigned short clientPorts[2];
        bool clientConnected[2];

        unsigned int lastTimestamps[2];
        short playerInputs[2];

        std::vector<unsigned int> processedCriticalPackets[2];

        // ultimo id que procesamos para reconciliar
        unsigned int lastProcessedMovementID[2];

        S2C_StatePacket worldState;
        sf::Clock serverClock;
        sf::Clock endClock;
        sf::Clock inactivityClock[2];
        bool pendingDelete;
        bool timeoutWarningShown[2];
    };

    unsigned short port;
    bool isRunning;
    sf::UdpSocket udpSocket;

    std::map<unsigned int, MatchData> matches;

    // movidas de los hilos
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::mutex dataMutex; // candado principal del mapa
    std::condition_variable condition;
    bool stopPool;

    void ThreadPoolWorker();
    void EnqueueTask(std::function<void()> task);

    // funciones del server
    void ProcessPackets();
    void UpdatePhysics(MatchData& match);
    void BroadcastState(MatchData& match);
    void InitMatch(MatchData& match, unsigned int matchID);
    void CleanupMatches();
    void CheckTimeouts(MatchData& match);

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();

    void CheckCollisions(MatchData& match);
    void HandlePlayerDeath(MatchData& match, short playerIndex, short killerID);
};