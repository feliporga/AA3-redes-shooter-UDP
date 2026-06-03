#pragma once
#include <SFML/Network.hpp>
#include <SFML/System.hpp>
#include <iostream>
#include <optional>
#include <map>
#include <vector>
#include <string>
#include "NetworkProtocol.h" 
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>

// Tiempo maximo sin saber nada de un jugador antes de hacerlo perder
#define PLAYER_TIMEOUT_SECONDS 5
// Este tiempo marca cuando aparece el aviso por consola
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

        // Cada jugador acumula sus propias irregularidades dentro de esta partida
        short irregularities[2];
        unsigned short irregularityEventID;

        S2C_StatePacket worldState;
        sf::Clock serverClock;
        sf::Clock endClock;
        // Este reloj mide cuanto lleva cada jugador sin mandar nada
        sf::Clock inactivityClock[2];
        bool pendingDelete;
        // Esto evita repetir el aviso por consola muchas veces seguidas
        bool timeoutWarningShown[2];
    };

    unsigned short port;
    bool isRunning;
    sf::UdpSocket udpSocket;

    std::map<unsigned int, MatchData> matches;

    // Los hilos usan estos datos para coger tareas pendientes
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::mutex dataMutex; // Protege las partidas mientras varios hilos las usan.
    std::condition_variable condition;
    bool stopPool;

    void ThreadPoolWorker();
    void EnqueueTask(std::function<void()> task);

    // Estas son las funciones principales del servidor UDP
    void ProcessPackets();
    void UpdatePhysics(MatchData& match);
    void BroadcastState(MatchData& match);
    void InitMatch(MatchData& match, unsigned int matchID);
    void CleanupMatches();
    void CheckTimeouts(MatchData& match);

    std::vector<std::vector<short>> levelMap;
    short mapRows = 0;
    short mapCols = 0;

    void LoadMapCollisions();
    bool CheckTileCollision(short x, short y, short w, short h);

    // Registra una irregularidad avisa a los clientes y termina la partida si llega a tres
    void RegisterIrregularity(MatchData& match, short playerIndex, const std::string& reason);

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();

    void CheckCollisions(MatchData& match);
    void HandlePlayerDeath(MatchData& match, short playerIndex, short killerID);
};