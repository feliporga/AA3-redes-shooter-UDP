#include "Server.h"
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>

Server::Server(unsigned short port) : port(port), isRunning(false), stopPool(false) {
    // arrancamos los 4 trabajadores
    for (short i = 0; i < 4; ++i) {
        workers.emplace_back(&Server::ThreadPoolWorker, this);
    }
}

Server::~Server() {
    Stop();
    // paramos el pool limpio
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stopPool = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

// bucle de los currantes esperando faena
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

void Server::InitMatch(MatchData& match, unsigned int matchID) {
    for (short i = 0; i < 2; i++) {
        match.clientIPs[i].reset();
        match.clientPorts[i] = 0;
        match.clientConnected[i] = false;
        match.lastTimestamps[i] = 0;
        match.timeoutWarningShown[i] = false;
        match.playerInputs[i] = INPUT_NONE;
        match.processedCriticalPackets[i].clear();

        // iniciamos a cero para la reconciliacion
        match.lastProcessedMovementID[i] = 0;

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

    // apagamos balas
    for (short i = 0; i < MAX_BULLETS; i++) {
        match.worldState.bullets[i].active = false;
    }

    match.serverClock.restart();
    match.endClock.restart();
    match.pendingDelete = false;
}

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

            // pilla de red a todo trapo
            ProcessPackets();

            // bloqueamos para que los hilos no pisen las fisicas
            std::unique_lock<std::mutex> lock(dataMutex);
            for (auto& pair : matches) {
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

void Server::ProcessPackets() {
    char buffer[1024];
    std::size_t received;
    std::optional<sf::IpAddress> sender;
    unsigned short senderPort;

    while (udpSocket.receive(buffer, sizeof(buffer), received, sender, senderPort) == sf::Socket::Status::Done) {
        if (received < sizeof(C2S_InputPacket)) continue;

        uint8_t flags = buffer[0];

        C2S_InputPacket in;
        std::memcpy(&in, buffer, sizeof(C2S_InputPacket));

        if (in.matchID == 0) continue;

        // le tiramos el curro a la pool
        EnqueueTask([this, in, flags, sender, senderPort]() {

            std::unique_lock<std::mutex> lock(dataMutex);

            if (matches.find(in.matchID) == matches.end()) {
                InitMatch(matches[in.matchID], in.matchID);
                std::cout << "[UDP SERVER] sala creada match " << in.matchID << std::endl;
            }

            MatchData& match = matches[in.matchID];

            // miramos bitmask critico
            if (flags & CRITICAL_PACKET) {
                S2C_AckPacket ack;
                ack.ackedPacketID = in.packetID;
                udpSocket.send(&ack, sizeof(S2C_AckPacket), sender.value(), senderPort);

                short idx = in.playerID - 1;

                if (idx >= 0 && idx < 2) {
                    bool alreadyProcessed =
                        std::find(
                            match.processedCriticalPackets[idx].begin(),
                            match.processedCriticalPackets[idx].end(),
                            in.packetID
                        ) != match.processedCriticalPackets[idx].end();

                    if (!alreadyProcessed) {
                        match.processedCriticalPackets[idx].push_back(in.packetID);

                        if (in.inputMask & INPUT_SHOOT) {
                            // buscar bala libre
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
                }
            }

            if (in.type == (short)PacketType::MatchCreatedNotification) {
                for (short i = 0; i < 2; i++) {
                    match.clientConnected[i] = false;
                    match.worldState.players[i].posX = 100 + (i * 400);
                    match.worldState.players[i].posY = 300;
                }
            }
            else if (in.type == (short)PacketType::GameMove) {
                short idx = in.playerID - 1;
                if (idx >= 0 && idx < 2) {
                    match.inactivityClock[idx].restart();
                    match.timeoutWarningShown[idx] = false;

                    if (!match.clientConnected[idx]) {
                        match.clientIPs[idx] = sender;
                        match.clientPorts[idx] = senderPort;
                        match.clientConnected[idx] = true;
                        std::cout << "[UDP] jugador " << in.playerID << " enganchado" << std::endl;
                    }

                    if (!(flags & CRITICAL_PACKET)) {
                        if (in.timestamp >= match.lastTimestamps[idx]) {
                            match.lastTimestamps[idx] = in.timestamp;
                            match.playerInputs[idx] = in.inputMask;

                            if (in.movementID > match.lastProcessedMovementID[idx]) {
                                match.lastProcessedMovementID[idx] = in.movementID;
                                match.worldState.players[idx].lastProcessedMovementID = in.movementID;
                            }
                        }
                    }
                }
            }
            });
    }
}

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
        p.posY += p.velY;

        if (p.posY >= 500) { p.posY = 500; p.velY = 0; p.onGround = true; }
    }

    // mover balas
    for (short i = 0; i < MAX_BULLETS; i++) {
        if (match.worldState.bullets[i].active) {
            match.worldState.bullets[i].posX += match.worldState.bullets[i].velX;

            // nos cargamos la bala si se va del mapa
            if (match.worldState.bullets[i].posX < -100 || match.worldState.bullets[i].posX > 2000) {
                match.worldState.bullets[i].active = false;
            }
        }
    }

    CheckCollisions(match);
}

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

                    // caja de colision aabb
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
    for (auto it = matches.begin(); it != matches.end(); ) {
        // borramos la sala al de un rato
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
    if (match.worldState.isGameOver) return;

    // pasamos si no han entrado los dos
    if (!match.clientConnected[0] || !match.clientConnected[1]) return;

    for (short i = 0; i < 2; i++) {
        float inactivitySeconds = match.inactivityClock[i].getElapsedTime().asSeconds();

        if (inactivitySeconds > PLAYER_WARNING_SECONDS && !match.timeoutWarningShown[i]) {
            match.timeoutWarningShown[i] = true;
            std::cout << "[UDP] jugador " << match.worldState.players[i].id << " afk avisando" << std::endl;
        }

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