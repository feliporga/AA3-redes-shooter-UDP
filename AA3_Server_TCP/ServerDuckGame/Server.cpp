#include "Server.h"

Server::Server(unsigned short port) : port(port), isRunning(false), driver(nullptr), con(nullptr), stopPool(false) {
    // Se crean hilos para procesar paquetes sin parar el servidor
    for (short i = 0; i < 4; ++i) {
        workers.emplace_back(&Server::ThreadPoolWorker, this);
    }
}

Server::~Server() {
    Stop();
    // Se avisa a los hilos antes de cerrar el servidor
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stopPool = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

// Cada hilo espera hasta que haya una tarea disponible
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

// Se añade la tarea a la cola y se despierta un hilo libre
void Server::EnqueueTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.push(task);
    }
    condition.notify_one();
}

// Se abre la conexion con la base de datos de login y ranking
bool Server::ConnectDatabase() {
    try {
        driver = get_driver_instance();
        std::string dbHost = std::string(SERVER_IP) + ":3306";
        con = driver->connect(dbHost, DB_USER, DB_PASS);
        con->setSchema(DBNAME);
        std::cout << "[BD] Conectado a " << DBNAME << " con exito" << std::endl;
        return true;
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] Error " << e.what() << std::endl;
        return false;
    }
}

// El bucle principal TCP acepta clientes y revisa el matchmaking
void Server::Run() {
    if (!ConnectDatabase()) return;

    if (listener.listen(port) != sf::Socket::Status::Done) {
        std::cout << "[TCP ERROR] Could not bind listener to port " << port << std::endl;
        return;
    }

    std::cout << "[SERVER TCP] Matchmaking and Launch server active on port " << port << std::endl;
    selector.add(listener);
    isRunning = true;

    while (isRunning) {
        if (selector.wait(sf::milliseconds(10))) {
            if (selector.isReady(listener)) {
                std::unique_lock<std::mutex> lock(dataMutex);
                AcceptNewClient();
            }
            else {
                HandleClientPackets();
            }
        }
        {
            std::unique_lock<std::mutex> lock(dataMutex);
            ProcessMatchmaking();
        }
    }
}

// El cliente nuevo queda aceptado y listo para recibir paquetes
void Server::AcceptNewClient() {
    sf::TcpSocket* newClient = new sf::TcpSocket();
    if (listener.accept(*newClient) == sf::Socket::Status::Done) {
        newClient->setBlocking(false);
        clients.push_back(newClient);
        selector.add(*newClient);
        std::cout << "[TCP] Nuevo cliente conectado" << std::endl;
    }
    else {
        delete newClient;
    }
}

// Los mensajes TCP se leen y se procesan segun su tipo
void Server::HandleClientPackets() {
    std::unique_lock<std::mutex> lock(dataMutex);
    for (short i = 0; i < (short)clients.size(); ) {
        sf::TcpSocket* client = clients[i];

        if (selector.isReady(*client)) {
            sf::Packet packet;
            sf::Socket::Status status = client->receive(packet);

            if (status == sf::Socket::Status::Done) {
                // El paquete pasa a un hilo para no bloquear el servidor
                EnqueueTask([this, client, packet]() mutable {
                    short typeInt;
                    if (packet >> typeInt) {
                        PacketType type = static_cast<PacketType>(typeInt);

                        // Se comprueba que el cliente siga conectado antes de responder
                        bool clientExists = false;
                        {
                            std::unique_lock<std::mutex> safeLock(dataMutex);
                            for (sf::TcpSocket* c : clients) {
                                if (c == client) clientExists = true;
                            }
                        }
                        if (!clientExists) return;

                        if (type == PacketType::LoginRequest) {
                            std::string u, p; packet >> u >> p;
                            // Usuario y contraseña se revisan en la base de datos
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
                            // El resultado llega desde el cliente
                            bool hasWon;
                            packet >> hasWon;

                            // Los nombres se guardan para actualizar luego la base de datos
                            std::string winnerName;
                            std::string loserName;

                            // Los datos compartidos se leen y el bloqueo se suelta pronto
                            {
                                std::unique_lock<std::mutex> safeLock(dataMutex);
                                // Solo cuenta el mensaje del ganador
                                if (!competitivePlayers[client] || !hasWon) return;
                                // Si el resultado ya esta contado no suma ni resta otra vez
                                if (reportedResults[client]) return;
                                reportedResults[client] = true;

                                winnerName = loggedInUsers[client];
                                loserName = opponentUsers[client];
                            }

                            if (!winnerName.empty() && !loserName.empty()) {
                                try {
                                    std::unique_lock<std::mutex> dbLock(dataMutex);

                                    // El ganador suma puntos y una victoria
                                    sql::PreparedStatement* winnerStmt = con->prepareStatement(
                                        "UPDATE users SET points = points + 25, wins = wins + 1 WHERE userName = ?"
                                    );
                                    winnerStmt->setString(1, winnerName);
                                    winnerStmt->executeUpdate();
                                    delete winnerStmt;

                                    // El perdedor pierde puntos y suma una derrota
                                    sql::PreparedStatement* loserStmt = con->prepareStatement(
                                        "UPDATE users SET points = GREATEST(points - 25, 0), losses = losses + 1 WHERE userName = ?"
                                    );
                                    loserStmt->setString(1, loserName);
                                    loserStmt->executeUpdate();
                                    delete loserStmt;

                                    std::cout << "[ELO] actualizados los puntos en la db" << std::endl;
                                }
                                catch (sql::SQLException& e) {
                                    std::cout << "[DATABASE ERROR] no se pudo actualizar elo" << std::endl;
                                }
                            }
                        }
                        else {
                            // Las listas y salas se protegen mientras cambian
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
                                std::cout << "[MATCHMAKING] jugador en cola casual" << std::endl;
                            }
                            else if (type == PacketType::SearchCompetitiveRequest) {
                                short playerElo;
                                packet >> playerElo;
                                compQueue.push_back({ client, playerElo });
                                std::cout << "[MATCHMAKING] jugador en cola competitiva" << std::endl;
                            }
                            else if (type == PacketType::CancelSearchRequest) {
                                RemoveFromQueues(client);
                                std::cout << "[MATCHMAKING] busqueda cancelada" << std::endl;
                            }
                            else if (type == PacketType::CheckMapVersion) {
                                short clientVersion; packet >> clientVersion;
                                HandleMapCheck(client, clientVersion);
                            }
                        }
                    }
                    });
                i++;
            }
            else if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) {
                std::cout << "[TCP] cliente desconectado" << std::endl;
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
                    std::cout << "[MATCH] partida lista mandando al udp" << std::endl;
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
    std::cout << "[SERVER] apagado" << std::endl;
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

    std::cout << "[SERVER] sala creada" << std::endl;

    sf::Packet response;
    response << (short)PacketType::RoomSuccess;
    client->send(response);
}

// El ranking se carga desde MySQL y se envia al cliente
void Server::SendRankingToClient(sf::TcpSocket* client) {
    try {
        std::string currentUser = loggedInUsers[client];

        //std::unique_lock<std::mutex> lock(dataMutex);
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
        std::cout << "[SERVER] ranking enviado" << std::endl;
    }
    catch (sql::SQLException& e) {
        std::cout << "[BD] error enviando ranking" << std::endl;
    }
}

// La comprobacion de contraseña se hace fuera del bloqueo para no frenar al servidor.
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
        std::cout << "[BD] error sql en login" << std::endl;
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
        std::cout << "[BD] error sql en registro" << std::endl;
        return false;
    }
}

void Server::DisconnectDatabase() {
    if (con != nullptr) {
        con->close();
        delete con;
        con = nullptr;
        std::cout << "[BD] conexion cerrada" << std::endl;
    }
}

// Los jugadores se emparejan desde las colas cuando hay pareja
void Server::ProcessMatchmaking() {
    while (casualQueue.size() >= 2) {
        sf::TcpSocket* p1 = casualQueue[0];
        sf::TcpSocket* p2 = casualQueue[1];
        casualQueue.erase(casualQueue.begin(), casualQueue.begin() + 2);

        std::cout << "[MATCHMAKING] partida casual encontrada" << std::endl;
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

                    std::cout << "[MATCHMAKING] partida competitiva encontrada" << std::endl;
                    StartGameForPlayers(p1, p2, true);
                    return;
                }
            }
        }
    }
}

void Server::StartGameForPlayers(sf::TcpSocket* p1, sf::TcpSocket* p2, bool isCompetitive) {
    // Se guarda si esta partida cuenta para ranking
    competitivePlayers[p1] = isCompetitive;
    competitivePlayers[p2] = isCompetitive;

    reportedResults[p1] = false;
    reportedResults[p2] = false;
    // Se guarda el rival para poder cambiar los puntos de los dos
    opponentUsers[p1] = loggedInUsers[p2];
    opponentUsers[p2] = loggedInUsers[p1];

    unsigned short matchID = nextMatchID++;

    std::cout << "[MATCHMAKING] iniciando servidor udp para matchid " << matchID << std::endl;

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

// El cliente se elimina de las colas si cancela o se desconecta
void Server::RemoveFromQueues(sf::TcpSocket* client) {
    for (std::vector<sf::TcpSocket*>::iterator it = casualQueue.begin(); it != casualQueue.end(); ) {
        if (*it == client) it = casualQueue.erase(it);
        else ++it;
    }
    for (std::vector<CompPlayer>::iterator it = compQueue.begin(); it != compQueue.end(); ) {
        if (it->socket == client) it = compQueue.erase(it);
        else ++it;
    }
}

// Se comprueba la version del mapa que tiene el cliente
void Server::HandleMapCheck(sf::TcpSocket* client, short clientVersion) {
    short officialServerVersion = 1;

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
        std::cout << "[LAUNCHER] cliente desactualizado mandando mapa nuevo" << std::endl;
    }
    else {
        sf::Packet res;
        res << (short)PacketType::MapUpToDate;
        client->send(res);
        std::cout << "[LAUNCHER] cliente tiene la version buena" << std::endl;
    }
}