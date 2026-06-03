#include "Server.h"
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>

Server::Server(unsigned short port) : port(port), isRunning(false), stopPool(false) {
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

    // La partida empieza sin balas activas
    for (short i = 0; i < MAX_BULLETS; i++) {
        match.worldState.bullets[i].active = false;
    }

    match.serverClock.restart();
    match.endClock.restart();
    match.pendingDelete = false;
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

            // Los paquetes importantes se confirman con ACK para no repetirlos
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
                    // Un paquete recibido del jugador indica que sigue conectado
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

// Se mueven jugadores y balas y se revisan golpes
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