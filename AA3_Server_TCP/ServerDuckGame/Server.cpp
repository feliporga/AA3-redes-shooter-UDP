#include "Server.h"
#include "bcrypt.h"

Server::Server(unsigned short port) : port(port), isRunning(false), driver(nullptr), con(nullptr), stopPool(false) {
    // --- INICIALIZAR LA THREAD POOL ---
    for (short i = 0; i < 4; ++i) { // Optimizado a short
        workers.emplace_back(&Server::ThreadPoolWorker, this);
    }
}

Server::~Server() {
    Stop();
    // --- APAGAR THREAD POOL ---
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stopPool = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

// --- LÓGICA DEL HILO TRABAJADOR ---
void Server::ThreadPoolWorker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stopPool || !tasks.empty(); });
            if (stopPool && tasks.empty()) return;
            task = std::move(tasks.front());
            tasks.pop();
        }
        task();
    }
}

// --- FUNCIÓN PARA METER TAREAS A LA COLA ---
void Server::EnqueueTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.push(task);
    }
    condition.notify_one();
}

bool Server::ConnectDatabase() {
    try {
        driver = get_driver_instance();
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
        std::cout << "[TCP ERROR] Could not bind listener to port " << port << std::endl;
        return;
    }

    std::cout << "[SERVER TCP] Matchmaking and Launch server active on port " << port << "..." << std::endl;
    selector.add(listener);
    isRunning = true;

    while (isRunning) {
        if (selector.wait(sf::milliseconds(10))) {
            if (selector.isReady(listener)) {
                std::unique_lock<std::mutex> lock(dataMutex); // Bloqueo al añadir cliente
                AcceptNewClient();
            }
            else {
                HandleClientPackets();
            }
        }
        {
            std::unique_lock<std::mutex> lock(dataMutex); // Bloqueo al gestionar colas
            ProcessMatchmaking();
        }
    }
}

void Server::AcceptNewClient() {
    sf::TcpSocket* newClient = new sf::TcpSocket();
    if (listener.accept(*newClient) == sf::Socket::Status::Done) {
        newClient->setBlocking(false);
        clients.push_back(newClient);
        selector.add(*newClient);
        std::cout << "[TCP] Nuevo cliente desde: " << newClient->getRemoteAddress()->toString() << std::endl;
    }
    else {
        delete newClient;
    }
}

void Server::HandleClientPackets() {
    std::unique_lock<std::mutex> lock(dataMutex); // Protegemos la iteración

    for (short i = 0; i < (short)clients.size(); ) {
        sf::TcpSocket* client = clients[i];

        if (selector.isReady(*client)) {
            sf::Packet packet;
            sf::Socket::Status status = client->receive(packet);

            if (status == sf::Socket::Status::Done) {
                // --- ENVIAMOS EL PAQUETE A LA THREAD POOL ---
                EnqueueTask([this, client, packet]() mutable {
                    short typeInt;
                    if (packet >> typeInt) {
                        PacketType type = static_cast<PacketType>(typeInt);

                        // SEGURIDAD: Verificamos si el cliente sigue conectado
                        bool clientExists = false;
                        {
                            std::unique_lock<std::mutex> safeLock(dataMutex);
                            for (auto* c : clients) {
                                if (c == client) clientExists = true;
                            }
                        }
                        if (!clientExists) return;

                        if (type == PacketType::LoginRequest) {
                            std::string u, p; packet >> u >> p;
                            bool ok = CheckUserLogin(u, p);

                            std::unique_lock<std::mutex> safeLock(dataMutex);
                            sf::Packet res;
                            if (ok) {
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
                            bool ok = RegisterUser(u, p);

                            std::unique_lock<std::mutex> safeLock(dataMutex);
                            sf::Packet res;
                            if (ok) res << (short)PacketType::RegisterSuccess;
                            else res << (short)PacketType::RegisterFailed;
                            client->send(res);
                        }
                        else if (type == PacketType::ReportResult) {
                            bool hasWon;
                            packet >> hasWon;

                            std::string username;
                            {
                                std::unique_lock<std::mutex> safeLock(dataMutex);
                                if (!competitivePlayers[client]) return;
                                if (reportedResults[client]) return;
                                reportedResults[client] = true;
                                username = loggedInUsers[client];
                            }

                            if (!username.empty()) {
                                try {
                                    // Bloqueamos la conexión MySQL para que no colisionen las queries
                                    std::unique_lock<std::mutex> dbLock(dataMutex);
                                    sql::PreparedStatement* pstmt;

                                    if (hasWon) {
                                        pstmt = con->prepareStatement("UPDATE users SET points = points + 25, wins = wins + 1 WHERE userName = ?");
                                    }
                                    else {
                                        pstmt = con->prepareStatement("UPDATE users SET points = GREATEST(points - 25, 0), losses = losses + 1 WHERE userName = ?");
                                    }

                                    pstmt->setString(1, username);
                                    pstmt->executeUpdate();
                                    delete pstmt;
                                    std::cout << "[DATABASE] ELO classification updated for user: " << username << std::endl;
                                }
                                catch (sql::SQLException& e) {
                                    std::cout << "[DATABASE ERROR] Could not update ELO: " << e.what() << std::endl;
                                }
                            }
                        }
                        else {
                            // Operaciones estándar bloqueadas por seguridad
                            std::unique_lock<std::mutex> safeLock(dataMutex);

                            if (type == PacketType::CreateRoomRequest) {
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
                            else if (type == PacketType::SearchCasualRequest) {
                                casualQueue.push_back(client);
                                std::cout << "[MATCHMAKING] Player joined Casual queue." << std::endl;
                            }
                            else if (type == PacketType::SearchCompetitiveRequest) {
                                short playerElo; packet >> playerElo;
                                compQueue.push_back({ client, playerElo });
                                std::cout << "[MATCHMAKING] Player joined Competitive queue. ELO: " << playerElo << std::endl;
                            }
                            else if (type == PacketType::CancelSearchRequest) {
                                RemoveFromQueues(client);
                                std::cout << "[MATCHMAKING] Player canceled search." << std::endl;
                            }
                            else if (type == PacketType::CheckMapVersion) {
                                short clientVersion; packet >> clientVersion; // OPTIMIZADO a short
                                HandleMapCheck(client, clientVersion);
                            }
                        }
                    }
                    });
                i++;
            }
            else if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) {
                std::cout << "[TCP] Cliente desconectado." << std::endl;
                RemoveFromQueues(client);
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

// BCRYPT SE EJECUTA FUERA DEL CANDADO PARA NO BLOQUEAR EL SERVIDOR
bool Server::CheckUserLogin(const std::string& user, const std::string& password) {
    std::string hashFromDB;
    try {
        std::unique_lock<std::mutex> lock(dataMutex);
        sql::PreparedStatement* stmt = con->prepareStatement("SELECT password FROM users WHERE userName = ?");
        stmt->setString(1, user);
        sql::ResultSet* res = stmt->executeQuery();

        if (res->next()) {
            hashFromDB = res->getString("password");
        }
        delete res;
        delete stmt;
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error SQL en Login: " << e.what() << std::endl;
        return false;
    }

    if (!hashFromDB.empty()) {
        std::string reconstructedHash = "$2b$10$" + hashFromDB;
        return bcrypt::validatePassword(password, reconstructedHash);
    }
    return false;
}

bool Server::RegisterUser(const std::string& user, const std::string& password) {
    try {
        short count = 0;
        {
            std::unique_lock<std::mutex> lock(dataMutex);
            sql::PreparedStatement* checkStmt = con->prepareStatement("SELECT COUNT(*) FROM users WHERE userName = ?");
            checkStmt->setString(1, user);
            sql::ResultSet* res = checkStmt->executeQuery();
            if (res->next()) count = (short)res->getInt(1);
            delete res;
            delete checkStmt;
        }

        if (count > 0) return false;

        std::string fullHash = bcrypt::generateHash(password);
        std::string hashForDB = fullHash.substr(7);

        {
            std::unique_lock<std::mutex> lock(dataMutex);
            sql::PreparedStatement* insertStmt = con->prepareStatement("INSERT INTO users (userName, password) VALUES (?, ?)");
            insertStmt->setString(1, user);
            insertStmt->setString(2, hashForDB);

            short rows = (short)insertStmt->executeUpdate();
            delete insertStmt;
            return rows > 0;
        }
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error SQL en Registro: " << e.what() << std::endl;
        return false;
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

                if (activeRooms[i].players.size() == 2) {
                    std::cout << "[MATCH] Partida lista. Enviando al servidor UDP..." << std::endl;
                    sf::TcpSocket* p1 = activeRooms[i].players[0];
                    sf::TcpSocket* p2 = activeRooms[i].players[1];
                    StartGameForPlayers(p1, p2, false);
                    activeRooms.erase(activeRooms.begin() + i);
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

    std::cout << "[SERVER] Sala '" << roomName << "' creada." << std::endl;

    sf::Packet response;
    response << (short)PacketType::RoomSuccess;
    client->send(response);
}

void Server::SendRankingToClient(sf::TcpSocket* client) {
    try {
        std::string currentUser = loggedInUsers[client];

        std::unique_lock<std::mutex> lock(dataMutex); // Protegemos las consultas MySQL concurrentes
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

void Server::DisconnectDatabase() {
    if (con != nullptr) {
        con->close();
        delete con;
        con = nullptr;
        std::cout << "[BD] Conexion cerrada." << std::endl;
    }
}

void Server::ProcessMatchmaking() {
    while (casualQueue.size() >= 2) {
        sf::TcpSocket* p1 = casualQueue[0];
        sf::TcpSocket* p2 = casualQueue[1];
        casualQueue.erase(casualQueue.begin(), casualQueue.begin() + 2);

        std::cout << "[MATCHMAKING] Casual match found!" << std::endl;
        StartGameForPlayers(p1, p2, false);
    }

    if (compQueue.size() >= 2) {
        for (short i = 0; i < (short)compQueue.size(); i++) {
            for (short j = i + 1; j < (short)compQueue.size(); j++) {
                if (std::abs(compQueue[i].elo - compQueue[j].elo) <= 50) {
                    sf::TcpSocket* p1 = compQueue[i].socket;
                    sf::TcpSocket* p2 = compQueue[j].socket;

                    compQueue.erase(compQueue.begin() + j);
                    compQueue.erase(compQueue.begin() + i);

                    std::cout << "[MATCHMAKING] Competitive match found!" << std::endl;
                    StartGameForPlayers(p1, p2, true);
                    return;
                }
            }
        }
    }
}

void Server::StartGameForPlayers(sf::TcpSocket* p1, sf::TcpSocket* p2, bool isCompetitive) {
    competitivePlayers[p1] = isCompetitive;
    competitivePlayers[p2] = isCompetitive;

    reportedResults[p1] = false;
    reportedResults[p2] = false;

    unsigned short matchID = nextMatchID++; // OPTIMIZADO a short

    std::cout << "[MATCHMAKING] Handoff to UDP Game Server initializing... MatchID: " << matchID << std::endl;

    for (short i = 0; i < 2; i++) {
        sf::Packet startP;
        startP << (short)PacketType::GameStart;
        startP << (short)(i + 1);
        startP << matchID;
        startP << std::string(SERVER_IP);
        startP << (unsigned short)SERVER_UDP_PORT;

        if (i == 0) p1->send(startP);
        else p2->send(startP);
    }
}

void Server::RemoveFromQueues(sf::TcpSocket* client) {
    for (auto it = casualQueue.begin(); it != casualQueue.end(); ) {
        if (*it == client) it = casualQueue.erase(it);
        else ++it;
    }
    for (auto it = compQueue.begin(); it != compQueue.end(); ) {
        if (it->socket == client) it = compQueue.erase(it);
        else ++it;
    }
}

void Server::HandleMapCheck(sf::TcpSocket* client, short clientVersion) {
    short officialServerVersion = 1; // OPTIMIZADO a short

    if (clientVersion < officialServerVersion) {
        std::string mapData =
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "0 0 0 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0\n"
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "0 0 1 1 1 0 0 0 0 0 0 0 0 0 1 1 1 0 0 0\n"
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n";

        sf::Packet res;
        res << (short)PacketType::UpdateMap << officialServerVersion << mapData;
        client->send(res);
        std::cout << "[LAUNCHER] Cliente desactualizado. Enviando mapa v" << officialServerVersion << std::endl;
    }
    else {
        std::cout << "[LAUNCHER] Cliente verificado con el mapa correcto." << std::endl;
    }
}