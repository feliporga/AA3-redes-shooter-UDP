#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>

#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include "NetworkProtocol.h"

#define INITIAL_TURN 1
#define NUM_ROWS 6
#define NUM_COLS 6
#define EMPTY_BOARD 0
#define PORT 56000
#define HOST "127.0.0.1:3306"
#define USER "root"
#define PSSWRD "enti"
#define DBNAME "tictactoe_db"
#define MAX_POINTS 30
#define MIN_POINTS 15
#define INCREMENT 1
#define OPPONENT_NUM 3
class Server {
private:
    unsigned short port;
    bool isRunning;
    sf::TcpListener listener;
    sf::SocketSelector selector;
    std::vector<sf::TcpSocket*> clients;
    std::map<sf::TcpSocket*, std::string> loggedInUsers;

    sql::Driver* driver;
    sql::Connection* con;
    std::string dbHost = HOST;
    std::string dbUser = USER;
    std::string dbPass = PSSWRD;
    std::string dbName = DBNAME;

    // Estructura para una sala de 3 en Raya
    struct Room {
        std::string name;
        sf::TcpSocket* player1 = nullptr;
        sf::TcpSocket* player2 = nullptr; 
        std::vector<sf::TcpSocket*> players;
        int board[NUM_ROWS][NUM_COLS]; // Tablero de 6x6 vacío
        int currentTurn = INITIAL_TURN;   
    };


    //SISTEMA DE RESULTADOS
    struct MatchResult {
        std::vector<int> placements; // IDs de los jugadores
    };

    struct OngoingMatch {
        std::string roomName;
        std::vector<std::string> realNames; // ID -> Nombre BD
        std::vector<MatchResult> reportedResults;
    };

    std::vector<OngoingMatch> activeMatches;

    
    void HandleMatchResult(sf::TcpSocket* client, const std::string& roomName, const std::vector<int>& placements);
    void UpdatePlayerStats(const std::string& user, int pointsOffset, int winOffset, int lossOffset);

    std::vector<Room> activeRooms;

    void HandleCreateRoom(sf::TcpSocket* client, const std::string& roomName);
    void HandleJoinRoom(sf::TcpSocket* client, const std::string& roomName);

    bool ConnectDatabase();
    void DisconnectDatabase();
    bool CheckUserLogin(const std::string& user, const std::string& password);
    bool RegisterUser(const std::string& user, const std::string& password);

    void AcceptNewClient();
    void HandleClientPackets();

    void SendRankingToClient(sf::TcpSocket* client);

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();
};