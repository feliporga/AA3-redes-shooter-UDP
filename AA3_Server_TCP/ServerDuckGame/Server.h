#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <fstream>
#include <sstream>

#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include "NetworkProtocol.h"
#include "bcrypt.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_TCP_PORT 60000
#define SERVER_UDP_PORT 60001

#define DB_USER "root"
#define DB_PASS "enti"
#define DBNAME "duckgame_db"

class Server {
private:
    unsigned short port;
    bool isRunning;
    sf::TcpListener listener;
    sf::SocketSelector selector;
    std::vector<sf::TcpSocket*> clients;
    std::map<sf::TcpSocket*, std::string> loggedInUsers;

    // se guardan los datos basicos del servidor
    unsigned short nextMatchID = 1;

    // se guarda la conexion con MySQL
    sql::Driver* driver;
    sql::Connection* con;

    // Cada sala guarda sus jugadores
    struct Room {
        std::string name;
        std::vector<sf::TcpSocket*> players;
    };
    std::vector<Room> activeRooms;

    // Colas usadas para buscar partida
    struct CompPlayer {
        sf::TcpSocket* socket;
        short elo;
    };

    std::vector<sf::TcpSocket*> casualQueue;
    std::vector<CompPlayer> compQueue;
    // Marca si el jugador esta en una partida de ranking o no
    std::map<sf::TcpSocket*, bool> competitivePlayers;
    // Evita que la misma partida cuente dos veces
    std::map<sf::TcpSocket*, bool> reportedResults;
    // Guarda el nombre del rival aunque se cierre el juego
    std::map<sf::TcpSocket*, std::string> opponentUsers;

    // Los hilos usan estos datos para coger tareas pendientes
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::mutex dataMutex; // Este mutex protege la memoria compartida entre hilos
    std::condition_variable condition;
    bool stopPool;

    // Funciones usadas por los hilos
    void ThreadPoolWorker();
    void EnqueueTask(std::function<void()> task);

    // Funciones de matchmaking
    void ProcessMatchmaking();
    void StartGameForPlayers(sf::TcpSocket* p1, sf::TcpSocket* p2, bool isCompetitive);
    void RemoveFromQueues(sf::TcpSocket* client);

    // funciones de uso interno del servidor
    bool ConnectDatabase();
    void DisconnectDatabase();
    bool CheckUserLogin(const std::string& user, const std::string& password);
    bool RegisterUser(const std::string& user, const std::string& password);

	// Funciones para procesar los paquetes recibidos
    void AcceptNewClient();
    void HandleClientPackets();
    void HandleCreateRoom(sf::TcpSocket* client, const std::string& roomName);
    void HandleJoinRoom(sf::TcpSocket* client, const std::string& roomName);
    void SendRankingToClient(sf::TcpSocket* client);

    // La version del mapa se guarda como short
    void HandleMapCheck(sf::TcpSocket* client, short clientVersion);

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();
};