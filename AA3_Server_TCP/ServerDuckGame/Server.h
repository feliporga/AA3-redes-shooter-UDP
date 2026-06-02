#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <map>

// librerias para usar los hilos
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

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

    // usamos short para ahorrar memoria
    unsigned short nextMatchID = 1;

    // conexion de la base de datos
    sql::Driver* driver;
    sql::Connection* con;

    // guardamos los jugadores de cada sala
    struct Room {
        std::string name;
        std::vector<sf::TcpSocket*> players;
    };
    std::vector<Room> activeRooms;

    // colas para buscar partida
    struct CompPlayer {
        sf::TcpSocket* socket;
        short elo;
    };

    std::vector<sf::TcpSocket*> casualQueue;
    std::vector<CompPlayer> compQueue;
    std::map<sf::TcpSocket*, bool> competitivePlayers;
    std::map<sf::TcpSocket*, bool> reportedResults;
    std::map<sf::TcpSocket*, std::string> opponentUsers;

    // variables del pool de hilos
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::mutex dataMutex; // candado para no pisar la memoria compartida
    std::condition_variable condition;
    bool stopPool;

    // funciones de los hilos
    void ThreadPoolWorker();
    void EnqueueTask(std::function<void()> task);

    // matchmaking
    void ProcessMatchmaking();
    void StartGameForPlayers(sf::TcpSocket* p1, sf::TcpSocket* p2, bool isCompetitive);
    void RemoveFromQueues(sf::TcpSocket* client);

    // movidas internas del server
    bool ConnectDatabase();
    void DisconnectDatabase();
    bool CheckUserLogin(const std::string& user, const std::string& password);
    bool RegisterUser(const std::string& user, const std::string& password);

    void AcceptNewClient();
    void HandleClientPackets();
    void HandleCreateRoom(sf::TcpSocket* client, const std::string& roomName);
    void HandleJoinRoom(sf::TcpSocket* client, const std::string& roomName);
    void SendRankingToClient(sf::TcpSocket* client);

    // cambiamos a short la version
    void HandleMapCheck(sf::TcpSocket* client, short clientVersion);

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();
};