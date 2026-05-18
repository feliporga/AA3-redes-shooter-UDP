#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <map>

#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include "NetworkProtocol.h"

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

    // Conexión MySQL
    sql::Driver* driver;
    sql::Connection* con;

    // Estructura de Sala para el Shooter 1v1
    struct Room {
        std::string name;
        std::vector<sf::TcpSocket*> players;
    };
    std::vector<Room> activeRooms;

    // Métodos de gestión interna
    bool ConnectDatabase();
    void DisconnectDatabase();
    bool CheckUserLogin(const std::string& user, const std::string& password);
    bool RegisterUser(const std::string& user, const std::string& password);

    void AcceptNewClient();
    void HandleClientPackets();
    void HandleCreateRoom(sf::TcpSocket* client, const std::string& roomName);
    void HandleJoinRoom(sf::TcpSocket* client, const std::string& roomName);
    void SendRankingToClient(sf::TcpSocket* client);

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();
};