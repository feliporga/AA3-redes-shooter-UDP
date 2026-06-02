#include "Server.h"
#include <cstring>
#include <chrono>
#include <algorithm>

Server::Server(unsigned short port) : port(port), isRunning(false), stopPool(false) {
    for (short i = 0; i < 4; ++i) {
        workers.emplace_back(&Server::ThreadPoolWorker, this);
    }
}

Server::~Server() {
    Stop();
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stopPool = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

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
// cola de tareas de threads
void Server::EnqueueTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.push(task);
    }
    condition.notify_one();
}

void Server::InitMatch(MatchData& match, unsigned short matchID) {
    for (short i = 0; i < 2; i++) { // Optimizado a short
        match.clientIPs[i].reset();
        match.clientPorts[i] = 0;
        match.clientConnected[i] = false;
        match.lastTimestamps[i] = 0;
        match.playerInputs[i] = INPUT_NONE;
        match.processedCriticalPackets[i].clear();

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

    for (short i = 0; i < MAX_BULLETS; i++) { // Optimizado a short
        match.worldState.bullets[i].active = false;
    }

    match.serverClock.restart();
    match.endClock.restart();
    match.pendingDelete = false;
}

void Server::Run() {
    if (udpSocket.bind(port) != sf::Socket::Status::Done) {
        std::cout << "[UDP ERROR] No se pudo abrir el puerto " << port << std::endl;
        return;
    }
    udpSocket.setBlocking(false);
    isRunning = true;
    std::cout << "[SERVER UDP] Escuchando gameplay en puerto " << port << " con Thread Pool..." << std::endl;

    const sf::Time timePerTick = sf::seconds(1.0f / 60.0f);
    sf::Clock tickClock;
    sf::Time timeSinceLastUpdate = sf::Time::Zero;

    while (isRunning) {
        timeSinceLastUpdate += tickClock.restart();
        while (timeSinceLastUpdate > timePerTick) {
            timeSinceLastUpdate -= timePerTick;

            // Recibe todo lo que hay en el puerto y lo mete en la cola de tareas
            ProcessPackets();

            std::unique_lock<std::mutex> lock(dataMutex);
            for (auto& pair : matches) {
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

        // Copiamos el paquete en memoria local
        C2S_InputPacket in;
        std::memcpy(&in, buffer, sizeof(C2S_InputPacket));

        if (in.matchID == 0) continue;

        // Capturamos 'in', 'flags', 'sender' y 'senderPort' por valor para que cada hilo tenga los suyos
        EnqueueTask([this, in, flags, sender, senderPort]() {

            // Usamos el dataMutex para proteger el mapa de partidas 'matches'
            std::unique_lock<std::mutex> lock(dataMutex);

            if (matches.find(in.matchID) == matches.end()) {
                InitMatch(matches[in.matchID], in.matchID);
                std::cout << "[UDP SERVER] Nueva partida creada. MatchID: " << in.matchID << std::endl;
            }

            MatchData& match = matches[in.matchID];

            // IDENTIFICACIÓN: BITMASK (CRÍTICO)
            if (flags & CRITICAL_PACKET) {
                std::cout << "[UDP] Paquete CRITICO recibido (ID: " << in.packetID << "). Enviando ACK." << std::endl;

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
                            for (short i = 0; i < MAX_BULLETS; i++) { // Optimizado a short
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
                    else {
                        std::cout << "[UDP] Paquete CRITICO duplicado ignorado." << std::endl;
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
                    if (!match.clientConnected[idx]) {
                        match.clientIPs[idx] = sender;
                        match.clientPorts[idx] = senderPort;
                        match.clientConnected[idx] = true;
                        std::cout << "[UDP] Jugador " << in.playerID << " enlazado desde " << sender->toString() << ":" << senderPort << std::endl;
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

        p.velY += 1;
        p.velX = 0;

        if (mask & INPUT_LEFT) { p.velX = -5; p.facingRight = false; }
        if (mask & INPUT_RIGHT) { p.velX = 5; p.facingRight = true; }
        if ((mask & INPUT_JUMP) && p.onGround) { p.velY = -15; p.onGround = false; }

        p.posX += p.velX;
        p.posY += p.velY;

        if (p.posY >= 500) { p.posY = 500; p.velY = 0; p.onGround = true; }
    }

    for (short i = 0; i < MAX_BULLETS; i++) { // Optimizado a short
        if (match.worldState.bullets[i].active) {
            match.worldState.bullets[i].posX += match.worldState.bullets[i].velX;
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
    for (short i = 0; i < MAX_BULLETS; i++) { // Optimizado a short
        if (match.worldState.bullets[i].active) {

            short bX = match.worldState.bullets[i].posX;
            short bY = match.worldState.bullets[i].posY;

            for (short p = 0; p < 2; p++) { // Optimizado a short
                if (match.worldState.bullets[i].ownerID != match.worldState.players[p].id) {

                    short pX = match.worldState.players[p].posX;
                    short pY = match.worldState.players[p].posY;

                    bool collisionX = (pX < bX + BULLET_HITBOX_W) && (pX + PLAYER_HITBOX_W > bX);
                    bool collisionY = (pY < bY + BULLET_HITBOX_H) && (pY + PLAYER_HITBOX_H > bY);

                    if (collisionX && collisionY) {
                        match.worldState.bullets[i].active = false;
                        match.worldState.players[p].hp -= BULLET_DAMAGE;

                        std::cout << "[UDP] IMPACTO! Jugador " << match.worldState.players[p].id
                            << " recibe dano. HP restante: " << match.worldState.players[p].hp << std::endl;

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
        std::cout << "[UDP] GAME OVER! Ganador: Jugador " << killerID << std::endl;
    }
    else {
        match.worldState.players[playerIndex].hp = INITIAL_HP;
        match.worldState.players[playerIndex].posX = RESPAWN_POS_X;
        match.worldState.players[playerIndex].posY = RESPAWN_POS_Y;
        match.worldState.players[playerIndex].velX = 0;
        match.worldState.players[playerIndex].velY = 0;
        std::cout << "[UDP] Jugador " << match.worldState.players[playerIndex].id
            << " muere y respawnea. Vidas restantes: " << match.worldState.players[playerIndex].lives << std::endl;
    }
}

void Server::CleanupMatches() {
    for (auto it = matches.begin(); it != matches.end(); ) {
        if (it->second.pendingDelete && it->second.endClock.getElapsedTime().asSeconds() > 5.0f) {
            std::cout << "[UDP] Eliminando partida finalizada. MatchID: " << it->first << std::endl;
            it = matches.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Server::Stop() {
    isRunning = false;
    matches.clear();
    udpSocket.unbind();
}