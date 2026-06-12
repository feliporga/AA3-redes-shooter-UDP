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

#define PLAYER_TIMEOUT_SECONDS 5
#define PLAYER_WARNING_SECONDS 2

// Sistema PING/PONG UDP como en la teoria: el servidor pregunta y el cliente responde.
#define PING_INTERVAL_SECONDS 1
#define PONG_TIMEOUT_SECONDS 5
#define PONG_WARNING_SECONDS 2

// Tolerancia de validacion de movimiento para permitir perdida de paquetes y pequeñas diferencias de simulacion
#define MOVEMENT_VALIDATION_TOLERANCE 15
#define MINIMUM_VALIDATION_TIME_TOLERANCE 16
#define MAXIMUM_VALIDATION_TIME_TOLERANCE 250

class Server {
private:
    struct ClientMovementSnapshot {
        unsigned short movementID;
        unsigned int timestamp;
        short inputMask;
        short posX;
        short posY;
    };

    struct MatchData {
        std::optional<sf::IpAddress> clientIPs[2];
        unsigned short clientPorts[2];
        bool clientConnected[2];

        unsigned int lastTimestamps[2];

        // Timestamp del ultimo movimiento que el servidor acepto como valido.
        unsigned int lastValidMovementTimestamp[2];
        short playerInputs[2];

        std::vector<unsigned int> processedCriticalPackets[2];

        unsigned int lastProcessedMovementID[2];

        // Movimientos predichos que envia el cliente para validar y poder hacer BACK
        std::vector<ClientMovementSnapshot> pendingMovements[2];
        short lastValidPosX[2];
        short lastValidPosY[2];

        // Mientras esta activo, el servidor esta esperando que el cliente acepte el BACK y vuelva a una posicion valida
        bool movementCorrectionPending[2];
        unsigned short correctionBackMovementID[2];

        // Cada jugador acumula sus propias irregularidades dentro de esta partida
        short irregularities[2];
        unsigned short irregularityEventID;

        S2C_StatePacket worldState;
        sf::Clock serverClock;
        sf::Clock endClock;

        // Sistema PING/PONG:
        // pingClock controla cada cuanto el servidor envia Ping.
        // pongClock mide cuanto tiempo hace que no recibimos Pong de cada jugador.
        // lastRTT guarda el ultimo RTT calculado y averageRTT guarda una media suavizada.
        sf::Clock pingClock;
        sf::Clock pongClock[2];
        bool waitingPong[2];
        unsigned short nextPingID[2];
        unsigned short lastPongID[2];
        unsigned int lastPingTimestamp[2];
        float lastRTT[2];
        float averageRTT[2];
        bool hasRTT[2];

        // Este reloj mide cuanto lleva cada jugador sin mandar nada.
        // Se mantiene por compatibilidad, pero el timeout real ahora lo controla el PONG.
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
    void SendPingPackets(MatchData& match);
    void RegisterPong(MatchData& match, short playerIndex, unsigned short pingID, unsigned int pingTimestamp);

    std::vector<std::vector<short>> levelMap;
    short mapRows = 0;
    short mapCols = 0;

    void LoadMapCollisions();
    bool CheckTileCollision(short x, short y, short w, short h);
    bool CheckGround(short x, short y, short w, short h);

    // Validacion de posiciones predichas y reconciliacion BACK
    void InsertMovementSnapshot(MatchData& match, short playerIndex, const ClientMovementSnapshot& movement);
    void ValidateMovements(MatchData& match);

    // Prepara un BACK. Si countAsIrregularity es false, se usa para respawn o paquetes antiguos y no suma falta.
    void SetBackCorrection(MatchData& match, short playerIndex, const std::string& reason, bool countAsIrregularity);

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