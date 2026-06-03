#include "Server.h"


Server::Server(unsigned short port) : port(port), isRunning(false), stopPool(false) {
    LoadMapCollisions();
    // Se crean varios hilos para repartir el trabajo
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

void Server::EnqueueTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.push(task);
    }
    condition.notify_one();
}

// Se prepara una partida nueva con jugadores vida balas y relojes limpios
void Server::InitMatch(MatchData& match, unsigned int matchID) {
    for (short i = 0; i < 2; i++) {
        match.clientIPs[i].reset();
        match.clientPorts[i] = 0;
        match.clientConnected[i] = false;
        match.lastTimestamps[i] = 0;
        match.timeoutWarningShown[i] = false;
        match.playerInputs[i] = INPUT_NONE;
        match.processedCriticalPackets[i].clear();

        // El jugador empieza sin movimientos confirmados
        match.lastProcessedMovementID[i] = 0;

        // Los jugadores empiezan sin irregularidades acumuladas
        match.irregularities[i] = 0;

        match.worldState.players[i].id = i + 1;
        match.worldState.players[i].posX = 100 + (i * 400);
        match.worldState.players[i].posY = 300;
        match.worldState.players[i].velX = 0;
        match.worldState.players[i].velY = 0;
        match.worldState.players[i].onGround = false;
        match.worldState.players[i].facingRight = (i == 0);

        match.worldState.players[i].isTaunting = false;

        match.worldState.players[i].hp = INITIAL_HP;
        match.worldState.players[i].lives = INITIAL_LIVES;
        match.worldState.players[i].lastProcessedMovementID = 0;
    }

    match.worldState.isGameOver = false;
    match.worldState.winnerID = 0;
    match.worldState.matchID = matchID;

    // La partida empieza sin avisos pendientes del anti-cheat
    match.irregularityEventID = 0;
    match.worldState.correctionRequired = false;
    match.worldState.irregularPlayerID = 0;
    match.worldState.irregularityCount = 0;
    match.worldState.irregularityEventID = 0;
    match.worldState.gameOverByIrregularities = false;

    // La partida empieza sin balas activas
    for (short i = 0; i < MAX_BULLETS; i++) {
        match.worldState.bullets[i].active = false;
    }

    match.serverClock.restart();
    match.endClock.restart();
    match.pendingDelete = false;
}

// El servidor registra una accion no permitida y envia la correccion a los clientes
void Server::RegisterIrregularity(MatchData& match, short playerIndex, const std::string& reason) {
    if (playerIndex < 0 || playerIndex >= 2) return;
    if (match.worldState.isGameOver) return;

    match.irregularities[playerIndex]++;
    match.irregularityEventID++;

    // El snapshot avisa a ambos clientes y permite que el jugador afectado se reconcilie
    match.worldState.correctionRequired = true;
    match.worldState.irregularPlayerID = match.worldState.players[playerIndex].id;
    match.worldState.irregularityCount = match.irregularities[playerIndex];
    match.worldState.irregularityEventID = match.irregularityEventID;

    // El input sospechoso se cancela y se conserva como valida la posicion del servidor
    match.playerInputs[playerIndex] = INPUT_NONE;

    std::cout << "[ANTI-CHEAT] jugador " << match.worldState.players[playerIndex].id
        << " irregularidad " << match.irregularities[playerIndex]
        << "/" << MAX_IRREGULARITIES << ": " << reason << std::endl;

    // Si acumula tres irregularidades pierde automaticamente la partida
    if (match.irregularities[playerIndex] >= MAX_IRREGULARITIES) {
        short winnerIndex = (playerIndex == 0) ? 1 : 0;

        match.worldState.isGameOver = true;
        match.worldState.winnerID = match.worldState.players[winnerIndex].id;
        match.worldState.gameOverByIrregularities = true;

        match.pendingDelete = true;
        match.endClock.restart();

        std::cout << "[ANTI-CHEAT] jugador " << match.worldState.players[playerIndex].id
            << " eliminado. Gana jugador " << match.worldState.winnerID << std::endl;
    }
}

// El bucle principal UDP lee paquetes actualiza partidas y envia estados
void Server::Run() {
    if (udpSocket.bind(port) != sf::Socket::Status::Done) {
        std::cout << "[UDP ERROR] fallo al abrir puerto " << port << std::endl;
        return;
    }
    udpSocket.setBlocking(false);
    isRunning = true;
    std::cout << "[SERVER UDP] escuchando gameplay en puerto " << port << std::endl;

    const sf::Time timePerTick = sf::seconds(1.0f / 60.0f);
    sf::Clock tickClock;
    sf::Time timeSinceLastUpdate = sf::Time::Zero;

    while (isRunning) {
        timeSinceLastUpdate += tickClock.restart();
        while (timeSinceLastUpdate > timePerTick) {
            timeSinceLastUpdate -= timePerTick;

            // Se leen los paquetes UDP que estaban pendientes
            ProcessPackets();

            // Las partidas se protegen mientras se actualizan fisicas y estados
            std::unique_lock<std::mutex> lock(dataMutex);
            for (std::pair<const unsigned int, MatchData>& pair : matches) {
                // Se comprueba si alguien se ha desconectado antes de seguir la partida
                CheckTimeouts(pair.second);
                UpdatePhysics(pair.second);
                BroadcastState(pair.second);
            }
            CleanupMatches();
            lock.unlock();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Los paquetes UDP recibidos actualizan su partida
void Server::ProcessPackets() {
    char buffer[1024];
    std::size_t received;
    std::optional<sf::IpAddress> sender;
    unsigned short senderPort;

    while (udpSocket.receive(buffer, sizeof(buffer), received, sender, senderPort) == sf::Socket::Status::Done) {
        if (received < sizeof(C2S_InputPacket)) continue;
        if (!sender.has_value()) continue;

        uint8_t flags = buffer[0];

        C2S_InputPacket in;
        std::memcpy(&in, buffer, sizeof(C2S_InputPacket));

        if (in.matchID == 0) continue;

        // El paquete pasa a un hilo para no frenar la recepcion UDP
        EnqueueTask([this, in, flags, sender, senderPort]() {

            std::unique_lock<std::mutex> lock(dataMutex);

            if (matches.find(in.matchID) == matches.end()) {
                InitMatch(matches[in.matchID], in.matchID);
                std::cout << "[UDP SERVER] sala creada match " << in.matchID << std::endl;
            }

            MatchData& match = matches[in.matchID];

            if (in.type == (short)PacketType::MatchCreatedNotification) {
                for (short i = 0; i < 2; i++) {
                    match.clientConnected[i] = false;
                    match.worldState.players[i].posX = 100 + (i * 400);
                    match.worldState.players[i].posY = 300;
                }
                return;
            }

            if (in.type != (short)PacketType::GameMove) return;

            // Se busca si el origen del paquete ya pertenece a uno de los jugadores conectados
            short senderPlayerIndex = -1;
            for (short i = 0; i < 2; i++) {
                if (match.clientConnected[i] &&
                    match.clientIPs[i].has_value() &&
                    match.clientIPs[i].value() == sender.value() &&
                    match.clientPorts[i] == senderPort) {
                    senderPlayerIndex = i;
                }
            }

            short idx = in.playerID - 1;

            // Un jugador ya conocido no puede enviar un identificador que no exista
            if (idx < 0 || idx >= 2) {
                if (senderPlayerIndex != -1) {
                    RegisterIrregularity(match, senderPlayerIndex, "playerID no valido");
                }
                return;
            }

            // Un jugador no puede enviar acciones utilizando el playerID de su rival
            if (senderPlayerIndex != -1 && senderPlayerIndex != idx) {
                RegisterIrregularity(match, senderPlayerIndex, "intento de controlar al rival");
                return;
            }

            if (!match.clientConnected[idx]) {
                match.clientIPs[idx] = sender;
                match.clientPorts[idx] = senderPort;
                match.clientConnected[idx] = true;
                std::cout << "[UDP] jugador " << in.playerID << " enganchado" << std::endl;
            }
            else if (!match.clientIPs[idx].has_value() ||
                match.clientIPs[idx].value() != sender.value() ||
                match.clientPorts[idx] != senderPort) {
                // Si un origen desconocido intenta usar un slot ocupado el paquete se rechaza
                // No se suma falta al jugador real porque puede tratarse de un tercero
                std::cout << "[ANTI-CHEAT] paquete rechazado para jugador "
                    << in.playerID << " desde origen no registrado" << std::endl;
                return;
            }

            // Un paquete recibido del jugador indica que sigue conectado
            match.inactivityClock[idx].restart();
            match.timeoutWarningShown[idx] = false;

            // Solo se aceptan paquetes normales o paquetes criticos definidos por el juego
            if (flags != NORMAL_PACKET && flags != CRITICAL_PACKET) {
                RegisterIrregularity(match, idx, "flags de paquete no validos");
                return;
            }

            // Los paquetes importantes se confirman con ACK para no repetirlos
            if (flags & CRITICAL_PACKET) {
                // Los paquetes criticos de este juego solo pueden representar un disparo
                if (in.packetID == 0 || in.inputMask != INPUT_SHOOT || in.movementID != 0) {
                    RegisterIrregularity(match, idx, "paquete critico no valido");
                    return;
                }

                S2C_AckPacket ack;
                ack.ackedPacketID = in.packetID;
                udpSocket.send(&ack, sizeof(S2C_AckPacket), sender.value(), senderPort);

                bool alreadyProcessed =
                    std::find(
                        match.processedCriticalPackets[idx].begin(),
                        match.processedCriticalPackets[idx].end(),
                        in.packetID
                    ) != match.processedCriticalPackets[idx].end();

                if (!alreadyProcessed) {
                    match.processedCriticalPackets[idx].push_back(in.packetID);

                    if (in.inputMask & INPUT_SHOOT) {
                        // Se busca un hueco libre para crear la bala
                        for (short i = 0; i < MAX_BULLETS; i++) {
                            if (!match.worldState.bullets[i].active) {
                                match.worldState.bullets[i].active = true;
                                match.worldState.bullets[i].ownerID = in.playerID;
                                match.worldState.bullets[i].posY = match.worldState.players[idx].posY;

                                if (match.worldState.players[idx].facingRight) {
                                    match.worldState.bullets[i].posX = match.worldState.players[idx].posX + 20;
                                    match.worldState.bullets[i].velX = 15;
                                }
                                else {
                                    match.worldState.bullets[i].posX = match.worldState.players[idx].posX - 20;
                                    match.worldState.bullets[i].velX = -15;
                                }
                                break;
                            }
                        }
                    }
                }

                return;
            }

            // El movimiento normal puede moverse saltar o hacer burla pero no disparar
            short allowedMovementMask = INPUT_LEFT | INPUT_RIGHT | INPUT_JUMP | INPUT_TAUNT;
            if ((in.inputMask & ~allowedMovementMask) != 0) {
                RegisterIrregularity(match, idx, "input de movimiento no permitido");
                return;
            }

            // El movimiento normal necesita ID para que luego el cliente pueda reconciliar
            if (in.movementID == 0) {
                RegisterIrregularity(match, idx, "movementID no valido");
                return;
            }

            if (in.timestamp >= match.lastTimestamps[idx]) {
                match.lastTimestamps[idx] = in.timestamp;
                match.playerInputs[idx] = in.inputMask;

                if (in.movementID > match.lastProcessedMovementID[idx]) {
                    match.lastProcessedMovementID[idx] = in.movementID;
                    match.worldState.players[idx].lastProcessedMovementID = in.movementID;
                }
            }
            });
    }
}

// colision balas/mapa/jugadores/muerte
void Server::UpdatePhysics(MatchData& match) {
    if (match.worldState.isGameOver) return;

    for (short i = 0; i < 2; i++) {
        PlayerState& p = match.worldState.players[i];
        short mask = match.playerInputs[i];

        p.isTaunting = (mask & INPUT_TAUNT) != 0;

        p.velY += 1; // gravedad
        p.velX = 0;

        if (mask & INPUT_LEFT) { p.velX = -5; p.facingRight = false; }
        if (mask & INPUT_RIGHT) { p.velX = 5; p.facingRight = true; }
        if ((mask & INPUT_JUMP) && p.onGround) { p.velY = -15; p.onGround = false; }

        p.posX += p.velX;
        if (CheckTileCollision(p.posX, p.posY, PLAYER_HITBOX_W, PLAYER_HITBOX_H)) {
			p.posX -= p.velX; // choque horizontal
            p.velX = 0;
        }

        // si estamos cayendo onGround siempre false
        p.onGround = false;

        p.posY += p.velY;
        if (CheckTileCollision(p.posX, p.posY, PLAYER_HITBOX_W, PLAYER_HITBOX_H)) {
			p.posY -= p.velY; // choque vertical

            // Si caemos y onGround es true
            if (p.velY > 0) {
                p.onGround = true;
            }
            p.velY = 0; // frenamos
        }

		// Si el jugador cae al vacio muere

        if (p.posY > 1000) {
            short rivalID = (i == 0) ? match.worldState.players[1].id : match.worldState.players[0].id;
            HandlePlayerDeath(match, i, rivalID);
        }
    }

    // Se mueven las balas activas
    for (short i = 0; i < MAX_BULLETS; i++) {
        if (match.worldState.bullets[i].active) {
            match.worldState.bullets[i].posX += match.worldState.bullets[i].velX;

            // La bala se elimina si sale demasiado lejos del mapa
            if (match.worldState.bullets[i].posX < -100 || match.worldState.bullets[i].posX > 2000) {
                match.worldState.bullets[i].active = false;
            }
        }
    }

    CheckCollisions(match);
}

// lee el txt
void Server::LoadMapCollisions() {
    levelMap.clear();
    mapRows = 0;
    mapCols = 0;

    std::ifstream file("mapa_servidor.txt");

    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            std::vector<short> row;
            std::stringstream ss(line);
            short tile;

            while (ss >> tile) {
                row.push_back(tile);
            }

            if (!row.empty()) {
                levelMap.push_back(row);
            }
        }
        file.close();

        mapRows = (short)levelMap.size();
        if (mapRows > 0) {
            mapCols = (short)levelMap[0].size();
        }
        std::cout << "[UDP] mapa cargado automaticamente. Dimensiones: " << mapCols << "x" << mapRows << std::endl;
    }
    else {
        std::cout << "[UDP ERROR] no se encontro mapa_servidor.txt" << std::endl;
    }
}

bool Server::CheckTileCollision(short x, short y, short w, short h) {
    if (mapRows == 0 || mapCols == 0) return false;

    short TILE_SIZE = 32;
    short leftTile = x / TILE_SIZE;
    short rightTile = (x + w - 1) / TILE_SIZE;
    short topTile = y / TILE_SIZE;
    short bottomTile = (y + h - 1) / TILE_SIZE;

    // topes de seguridad
    if (leftTile < 0) leftTile = 0;
    if (rightTile >= mapCols) rightTile = mapCols - 1;
    if (topTile < 0) topTile = 0;
    if (bottomTile >= mapRows) bottomTile = mapRows - 1;

    for (short i = topTile; i <= bottomTile; i++) {
        for (short j = leftTile; j <= rightTile; j++) {
            if (levelMap[i][j] == 1) {
                return true;
            }
        }
    }
    return false;
}

// El estado actual se envia a cada cliente
void Server::BroadcastState(MatchData& match) {
    match.worldState.timestamp = match.serverClock.getElapsedTime().asMilliseconds();

    for (short i = 0; i < 2; i++) {
        match.worldState.players[i].lastProcessedMovementID = match.lastProcessedMovementID[i];

        if (match.clientConnected[i] && match.clientIPs[i].has_value()) {
            udpSocket.send(&match.worldState, sizeof(S2C_StatePacket), match.clientIPs[i].value(), match.clientPorts[i]);
        }
    }
}

void Server::CheckCollisions(MatchData& match) {
    for (short i = 0; i < MAX_BULLETS; i++) {
        if (match.worldState.bullets[i].active) {

            short bX = match.worldState.bullets[i].posX;
            short bY = match.worldState.bullets[i].posY;

            for (short p = 0; p < 2; p++) {
                if (match.worldState.bullets[i].ownerID != match.worldState.players[p].id) {

                    short pX = match.worldState.players[p].posX;
                    short pY = match.worldState.players[p].posY;

                    // Se comprueba si la bala impacta contra el jugador
                    bool collisionX = (pX < bX + BULLET_HITBOX_W) && (pX + PLAYER_HITBOX_W > bX);
                    bool collisionY = (pY < bY + BULLET_HITBOX_H) && (pY + PLAYER_HITBOX_H > bY);

                    if (collisionX && collisionY) {
                        match.worldState.bullets[i].active = false;
                        match.worldState.players[p].hp -= BULLET_DAMAGE;

                        if (match.worldState.players[p].hp <= 0) {
                            HandlePlayerDeath(match, p, match.worldState.bullets[i].ownerID);
                        }
                        break;
                    }
                }
            }
        }
    }
}

// Se restan vidas y la partida acaba si llegan a cero
void Server::HandlePlayerDeath(MatchData& match, short playerIndex, short killerID) {
    match.worldState.players[playerIndex].lives -= 1;

    if (match.worldState.players[playerIndex].lives <= 0) {
        match.worldState.isGameOver = true;
        match.worldState.winnerID = killerID;
        match.pendingDelete = true;
        match.endClock.restart();
        std::cout << "[UDP] palmo del todo gana " << killerID << std::endl;
    }
    else {
        match.worldState.players[playerIndex].hp = INITIAL_HP;
        match.worldState.players[playerIndex].posX = RESPAWN_POS_X;
        match.worldState.players[playerIndex].posY = RESPAWN_POS_Y;
        match.worldState.players[playerIndex].velX = 0;
        match.worldState.players[playerIndex].velY = 0;
    }
}

void Server::CleanupMatches() {
    for (std::map<unsigned int, MatchData>::iterator it = matches.begin(); it != matches.end(); ) {
        // Se espera un poco antes de borrar la partida
        if (it->second.pendingDelete && it->second.endClock.getElapsedTime().asSeconds() > 5.0f) {
            std::cout << "[UDP] limpieza de la partida " << it->first << std::endl;
            it = matches.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Server::CheckTimeouts(MatchData& match) {
    // Si la partida ya acabo no hace falta mirar desconexiones
    if (match.worldState.isGameOver) return;

    // Las desconexiones se comprueban cuando ya estan los dos jugadores
    if (!match.clientConnected[0] || !match.clientConnected[1]) return;

    for (short i = 0; i < 2; i++) {
        float inactivitySeconds = match.inactivityClock[i].getElapsedTime().asSeconds();

        // Sale un aviso por consola antes de echar al jugador
        if (inactivitySeconds > PLAYER_WARNING_SECONDS && !match.timeoutWarningShown[i]) {
            match.timeoutWarningShown[i] = true;
            std::cout << "[UDP] jugador " << match.worldState.players[i].id << " afk avisando" << std::endl;
        }

        // Si el timeout se cumple el jugador pierde y gana el rival
        if (inactivitySeconds > PLAYER_TIMEOUT_SECONDS) {
            short winnerIndex = (i == 0) ? 1 : 0;

            match.worldState.isGameOver = true;
            match.worldState.winnerID = match.worldState.players[winnerIndex].id;

            match.pendingDelete = true;
            match.endClock.restart();

            std::cout << "[UDP] timeout tiramos al jugador " << match.worldState.players[i].id << std::endl;
            return;
        }
    }
}

void Server::Stop() {
    isRunning = false;
    matches.clear();
    udpSocket.unbind();
}