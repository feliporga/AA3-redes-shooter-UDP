#include "Server.h"
#include "bcrypt.h"

Server::Server(unsigned short port) : port(port), isRunning(false), driver(nullptr), con(nullptr) {}

Server::~Server() { Stop(); }

bool Server::ConnectDatabase() {
    try {
        driver = get_driver_instance();

        // Juntamos la IP maestra y el puerto de MySQL de forma normal
        std::string dbHost = std::string(SERVER_IP) + ":3306";

        con = driver->connect(dbHost, DB_USER, DB_PASS);
        con->setSchema(DBNAME);
        std::cout << "[BD] Conectado a " << DBNAME << " con exito." << std::endl;
        return true;
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error: " << e.what() << std::endl;
        return false;
    }
}

void Server::Run() {
    if (!ConnectDatabase()) return;

    if (listener.listen(port) != sf::Socket::Status::Done) {
        std::cout << "[TCP ERROR] No se pudo abrir el puerto " << port << std::endl;
        return;
    }

    std::cout << "[SERVER TCP] Matchmaking activo en el puerto " << port << "..." << std::endl;
    selector.add(listener);
    isRunning = true;

    while (isRunning) {
        if (selector.wait()) {
            if (selector.isReady(listener)) {
                AcceptNewClient();
            }
            else {
                HandleClientPackets();
            }
        }
    }
}

void Server::AcceptNewClient() {
    sf::TcpSocket* newClient = new sf::TcpSocket();
    if (listener.accept(*newClient) == sf::Socket::Status::Done) {
        newClient->setBlocking(false);
        clients.push_back(newClient);
        selector.add(*newClient);
        // SFML 3: getRemoteAddress devuelve un optional
        std::cout << "[TCP] Nuevo cliente desde: " << newClient->getRemoteAddress()->toString() << std::endl;
    }
    else {
        delete newClient;
    }
}

void Server::HandleClientPackets() {
    // Bucle clásico con índice short para evitar 'auto'
    for (short i = 0; i < (short)clients.size(); ) {
        sf::TcpSocket* client = clients[i];

        if (selector.isReady(*client)) {
            sf::Packet packet;
            sf::Socket::Status status = client->receive(packet);

            if (status == sf::Socket::Status::Done) {
                short typeInt;
                if (packet >> typeInt) {
                    PacketType type = static_cast<PacketType>(typeInt);

                    if (type == PacketType::LoginRequest) {
                        std::string u, p; packet >> u >> p;
                        sf::Packet res;
                        if (CheckUserLogin(u, p)) {
                            loggedInUsers[client] = u;
                            res << (short)PacketType::LoginSuccess;
                        }
                        else {
                            res << (short)PacketType::LoginFailed;
                        }
                        client->send(res);
                    }
                    else if (type == PacketType::RegisterRequest) {
                        std::string u, p; packet >> u >> p;
                        sf::Packet res;
                        if (RegisterUser(u, p)) res << (short)PacketType::RegisterSuccess;
                        else res << (short)PacketType::RegisterFailed;
                        client->send(res);
                    }
                    else if (type == PacketType::CreateRoomRequest) {
                        std::string rName; packet >> rName;
                        HandleCreateRoom(client, rName);
                    }
                    else if (type == PacketType::JoinRoomRequest) {
                        std::string rName; packet >> rName;
                        HandleJoinRoom(client, rName);
                    }
                    else if (type == PacketType::RankingRequest) {
                        SendRankingToClient(client);
                    }
                }
                i++;
            }
            else if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) {
                std::cout << "[TCP] Cliente desconectado." << std::endl;
                selector.remove(*client);
                loggedInUsers.erase(client);
                delete client;
                clients.erase(clients.begin() + i);
            }
            else { i++; }
        }
        else { i++; }
    }
}

void Server::HandleJoinRoom(sf::TcpSocket* client, const std::string& roomName) {
    short numRooms = (short)activeRooms.size();
    for (short i = 0; i < numRooms; i++) {
        if (activeRooms[i].name == roomName) {
            if (activeRooms[i].players.size() < 2) {
                activeRooms[i].players.push_back(client);

                sf::Packet res;
                res << (short)PacketType::RoomSuccess;
                client->send(res);

                // SALA LLENA (1v1) -> Handoff a UDP
                if (activeRooms[i].players.size() == 2) {
                    std::cout << "[MATCH] Partida lista. Enviando al servidor UDP..." << std::endl;
                    for (short j = 0; j < 2; j++) {
                        sf::Packet startP;
                        startP << (short)PacketType::GameStart;
                        startP << (short)(j + 1); // PlayerID (1 o 2)

                        // USAMOS TU IP MAESTRA
                        startP << std::string(SERVER_IP);
                        startP << (unsigned short)SERVER_UDP_PORT;

                        activeRooms[i].players[j]->send(startP);
                    }
                    activeRooms[i].players.clear();
                }
                return;
            }
        }
    }
    sf::Packet err;
    err << (short)PacketType::RoomError;
    client->send(err);
}

void Server::Stop() {
    isRunning = false;
    listener.close();
    selector.clear();

    short numClients = (short)clients.size();
    for (short i = 0; i < numClients; i++) {
        clients[i]->disconnect();
        delete clients[i];
    }
    clients.clear();
    DisconnectDatabase();
    std::cout << "[SERVER] Servidor apagado." << std::endl;
}

void Server::HandleCreateRoom(sf::TcpSocket* client, const std::string& roomName) {
    short numRooms = (short)activeRooms.size();
    for (short i = 0; i < numRooms; i++) {
        if (activeRooms[i].name == roomName) {
            sf::Packet response;
            response << (short)PacketType::RoomError;
            client->send(response);
            return;
        }
    }

    Room newRoom;
    newRoom.name = roomName;
    newRoom.players.push_back(client);
    activeRooms.push_back(newRoom);

    std::cout << "[SERVER] Sala '" << roomName << "' creada por " << loggedInUsers[client] << std::endl;

    sf::Packet response;
    response << (short)PacketType::RoomSuccess;
    client->send(response);
}

void Server::SendRankingToClient(sf::TcpSocket* client) {
    try {
        std::string currentUser = loggedInUsers[client];

        sql::Statement* stmt = con->createStatement();
        sql::ResultSet* res = stmt->executeQuery("SELECT userName, points, wins, losses FROM users ORDER BY points DESC, wins DESC LIMIT 10");

        sf::Packet response;
        response << (short)PacketType::RankingResponse;

        struct PlayerData { short pos; std::string name; short pts; short v; short d; };
        std::vector<PlayerData> ranking;
        short currentPos = 1;

        while (res->next()) {
            ranking.push_back({
                currentPos++,
                res->getString("userName"),
                (short)res->getInt("points"),
                (short)res->getInt("wins"),
                (short)res->getInt("losses")
                });
        }
        delete res;
        delete stmt;

        if (currentUser != "") {
            sql::PreparedStatement* pstmt = con->prepareStatement(
                "SELECT userName, points, wins, losses, "
                "(SELECT COUNT(*) + 1 FROM users WHERE points > u.points) AS myPos "
                "FROM users u WHERE userName = ?"
            );
            pstmt->setString(1, currentUser);

            sql::ResultSet* resMe = pstmt->executeQuery();
            if (resMe->next()) {
                ranking.push_back({
                    (short)resMe->getInt("myPos"),
                    resMe->getString("userName"),
                    (short)resMe->getInt("points"),
                    (short)resMe->getInt("wins"),
                    (short)resMe->getInt("losses")
                    });
            }
            delete resMe;
            delete pstmt;
        }

        response << (short)ranking.size();
        short numPlayers = (short)ranking.size();
        for (short i = 0; i < numPlayers; i++) {
            response << ranking[i].pos << ranking[i].name << ranking[i].pts << ranking[i].v << ranking[i].d;
        }

        client->send(response);
        std::cout << "[SERVER] Ranking enviado correctamente." << std::endl;

    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error SQL enviando ranking: " << e.what() << std::endl;
    }
}

bool Server::CheckUserLogin(const std::string& user, const std::string& password) {
    try {
        sql::PreparedStatement* stmt = con->prepareStatement("SELECT password FROM users WHERE userName = ?");
        stmt->setString(1, user);
        sql::ResultSet* res = stmt->executeQuery();

        bool loginSuccess = false;
        if (res->next()) {
            std::string hashFromDB = res->getString("password");
            std::string reconstructedHash = "$2b$10$" + hashFromDB;
            if (bcrypt::validatePassword(password, reconstructedHash)) {
                loginSuccess = true;
            }
        }
        delete res;
        delete stmt;
        return loginSuccess;
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error SQL en Login: " << e.what() << std::endl;
        return false;
    }
}

bool Server::RegisterUser(const std::string& user, const std::string& password) {
    try {
        sql::PreparedStatement* checkStmt = con->prepareStatement("SELECT COUNT(*) FROM users WHERE userName = ?");
        checkStmt->setString(1, user);
        sql::ResultSet* res = checkStmt->executeQuery();

        short count = 0;
        if (res->next()) count = (short)res->getInt(1);

        delete res;
        delete checkStmt;

        if (count > 0) return false;

        std::string fullHash = bcrypt::generateHash(password);
        std::string hashForDB = fullHash.substr(7);

        sql::PreparedStatement* insertStmt = con->prepareStatement("INSERT INTO users (userName, password) VALUES (?, ?)");
        insertStmt->setString(1, user);
        insertStmt->setString(2, hashForDB);

        short rows = (short)insertStmt->executeUpdate();
        delete insertStmt;

        return rows > 0;
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error SQL en Registro: " << e.what() << std::endl;
        return false;
    }
}

void Server::DisconnectDatabase() {
    if (con != nullptr) {
        con->close();
        delete con;
        con = nullptr;
        std::cout << "[BD] Conexion cerrada." << std::endl;
    }
}