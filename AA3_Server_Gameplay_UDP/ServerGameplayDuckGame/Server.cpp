#include "Server.h"


Server::Server(unsigned short port) : port(port), isRunning(false), stopPool(false) {
    LoadMapCollisions();
    // Se crean varios hilos para repartir el trabajo
    for (short i = 0; i < 4; i++) {
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

        // Todavia no hay ningun movimiento validado, asi que el primer paquete parte desde cero.
        match.lastValidMovementTimestamp[i] = 0;

        // PING/PONG empieza limpio para cada jugador
        match.pongClock[i].restart();
        match.waitingPong[i] = false;
        match.nextPingID[i] = 1;
        match.lastPongID[i] = 0;
        match.lastPingTimestamp[i] = 0;
        match.lastRTT[i] = 0.0f;
        match.averageRTT[i] = 0.0f;
        match.hasRTT[i] = false;

        match.timeoutWarningShown[i] = false;
        match.playerInputs[i] = INPUT_NONE;
        match.processedCriticalPackets[i].clear();

        // El jugador empieza sin movimientos confirmados
        match.lastProcessedMovementID[i] = 0;

        // Los movimientos predichos pendientes empiezan vacios
        match.pendingMovements[i].clear();

        // Al empezar la partida no hay correccion BACK pendiente
        match.movementCorrectionPending[i] = false;
        match.correctionBackMovementID[i] = 0;

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

        // Ultima posicion valida del servidor usada para poder hacer BACK/ID
        match.lastValidPosX[i] = match.worldState.players[i].posX;
        match.lastValidPosY[i] = match.worldState.players[i].posY;
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

    // La partida empieza sin BACK pendiente
    match.worldState.movementBackRequired = false;
    match.worldState.backPlayerID = 0;
    match.worldState.backMovementID = 0;
    match.worldState.backPosX = 0;
    match.worldState.backPosY = 0;

    // La partida empieza sin balas activas
    for (short i = 0; i < MAX_BULLETS; i++) {
        match.worldState.bullets[i].active = false;
    }

    match.serverClock.restart();
    match.endClock.restart();

    // El reloj de Ping empieza al crear la partida.
    match.pingClock.restart();

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

    // Si acumula tres irregularidades pierde  maticamente la partida
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

// Prepara la reconciliacion BACK. Sirve tanto para un movimiento BAD como para un respawn.
void Server::SetBackCorrection(MatchData& match, short playerIndex, const std::string& reason, bool countAsIrregularity) {
    if (playerIndex < 0 || playerIndex >= 2) return;
    if (match.worldState.isGameOver) return;

    PlayerState& p = match.worldState.players[playerIndex];

    // BACK:
    // El servidor devuelve al jugador a la ultima posicion valida confirmada.
    p.posX = match.lastValidPosX[playerIndex];
    p.posY = match.lastValidPosY[playerIndex];
    p.velX = 0;
    p.velY = 0;
    p.onGround = CheckGround(p.posX, p.posY, PLAYER_HITBOX_W, PLAYER_HITBOX_H);

    match.worldState.movementBackRequired = true;
    match.worldState.backPlayerID = p.id;
    match.worldState.backMovementID = (unsigned short)match.lastProcessedMovementID[playerIndex];
    match.worldState.backPosX = match.lastValidPosX[playerIndex];
    match.worldState.backPosY = match.lastValidPosY[playerIndex];

    // Mientras el cliente no haya vuelto a una posicion cercana al BACK, ignoraremos snapshots antiguos.
    match.movementCorrectionPending[playerIndex] = true;
    match.correctionBackMovementID[playerIndex] = (unsigned short)match.lastProcessedMovementID[playerIndex];

    // Si una posicion es BAD, anulamos el resto de movimientos pendientes como en la reconciliacion del tema.
    match.pendingMovements[playerIndex].clear();

    std::cout << "[RECONCILIACION] BACK jugador " << p.id
        << " ID " << match.worldState.backMovementID
        << " -> (" << match.worldState.backPosX << ", " << match.worldState.backPosY
        << ") Motivo: " << reason << std::endl;

    if (countAsIrregularity) {
        RegisterIrregularity(match, playerIndex, reason);
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
                // Sistema PING/PONG:
                // el servidor pregunta periodicamente y luego comprueba si ha llegado el Pong.
                SendPingPackets(pair.second);

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
        if (received < sizeof(uint8_t) + sizeof(short)) continue;
        if (!sender.has_value()) continue;

        uint8_t flags = buffer[0];

        short packetType;
        std::memcpy(&packetType, buffer + 1, sizeof(short));

        // PONG:
        // El cliente responde a los Ping del servidor. Este paquete no mueve al jugador,
        // solo demuestra que sigue vivo y permite calcular el RTT.
        if (received == sizeof(C2S_PongPacket) && packetType == (short)PacketType::Pong) {
            C2S_PongPacket pong;
            std::memcpy(&pong, buffer, sizeof(C2S_PongPacket));

            if (pong.matchID == 0) continue;

            // El PONG tambien se procesa en la pool para seguir el mismo flujo productor-consumidor.
            // ProcessPackets solo recibe bytes y hace memcpy; la logica queda en ProcessPongPacket.
            std::function<void()> processPongPacket = [this, pong, sender, senderPort]() {
                ProcessPongPacket(pong, sender, senderPort);
                };

            EnqueueTask(processPongPacket);
            continue;
        }

        if (received < sizeof(C2S_InputPacket)) continue;

        C2S_InputPacket in;
        std::memcpy(&in, buffer, sizeof(C2S_InputPacket));

        if (in.matchID == 0) continue;

        // Se prepara el procesamiento del paquete de input.
        // Los paquetes normales se encolan en la ThreadPool.
        // Los paquetes URGENT se procesan directamente para no esperar detras de movimientos normales.
        // El datagrama ya se ha convertido con memcpy a C2S_InputPacket.
        // La logica queda en ProcessInputPacket para evitar una lambda gigante con auto.
        std::function<void()> processInputPacket = [this, in, flags, sender, senderPort]() {
            ProcessInputPacket(in, flags, sender, senderPort);
            };

        if ((flags & URGENT_PACKET) != 0) {
            // Los paquetes urgentes no esperan en la cola de tareas.
            // Se procesan inmediatamente para que acciones como el disparo no se retrasen detras del movimiento normal.
            processInputPacket();
        }
        else {
            // El paquete pasa a un hilo para no frenar la recepcion UDP.
            EnqueueTask(processInputPacket);
        }
    }
}

void Server::ProcessPongPacket(
    const C2S_PongPacket& pong,
    const std::optional<sf::IpAddress>& sender,
    unsigned short senderPort
) {
    std::unique_lock<std::mutex> lock(dataMutex);

    if (matches.find(pong.matchID) == matches.end()) {
        return;
    }

    MatchData& match = matches[pong.matchID];

    short idx = pong.playerID - 1;
    if (idx < 0 || idx >= 2) return;

    // Solo aceptamos el PONG si viene del mismo origen UDP que ya estaba registrado para ese jugador.
    if (!match.clientConnected[idx] ||
        !match.clientIPs[idx].has_value() ||
        match.clientIPs[idx].value() != sender.value() ||
        match.clientPorts[idx] != senderPort) {
        std::cout << "[PING/PONG] PONG rechazado por origen no registrado" << std::endl;
        return;
    }

    RegisterPong(match, idx, pong.pingID, pong.timestamp);
}

void Server::ProcessInputPacket(
    const C2S_InputPacket& in,
    uint8_t flags,
    const std::optional<sf::IpAddress>& sender,
    unsigned short senderPort
) {
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

            // Al reiniciar la sala, la ultima posicion valida vuelve al spawn
            match.lastValidPosX[i] = match.worldState.players[i].posX;
            match.lastValidPosY[i] = match.worldState.players[i].posY;
            match.pendingMovements[i].clear();
            match.lastProcessedMovementID[i] = 0;
            match.movementCorrectionPending[i] = false;

            // Al reiniciar partida, tambien se reinicia la referencia temporal de validacion.
            match.lastValidMovementTimestamp[i] = 0;
            match.correctionBackMovementID[i] = 0;

            // Tambien se reinicia el estado de PING/PONG y RTT.
            match.pongClock[i].restart();
            match.waitingPong[i] = false;
            match.nextPingID[i] = 1;
            match.lastPongID[i] = 0;
            match.lastPingTimestamp[i] = 0;
            match.lastRTT[i] = 0.0f;
            match.averageRTT[i] = 0.0f;
            match.hasRTT[i] = false;
            match.timeoutWarningShown[i] = false;
        }

        match.pingClock.restart();
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

        // Al enganchar el jugador empieza el control Ping/Pong para ese cliente.
        match.pongClock[idx].restart();
        match.waitingPong[idx] = false;
        match.nextPingID[idx] = 1;
        match.lastPongID[idx] = 0;
        match.lastPingTimestamp[idx] = 0;
        match.lastRTT[idx] = 0.0f;
        match.averageRTT[idx] = 0.0f;
        match.hasRTT[idx] = false;
        match.timeoutWarningShown[idx] = false;

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

    // Un paquete recibido del jugador indica actividad, pero el timeout real de conexion se valida con PING/PONG.
    match.inactivityClock[idx].restart();

    // FLAGS:
    // NORMAL_PACKET = 0, CRITICAL_PACKET = 1, URGENT_PACKET = 2.

    const uint8_t allowedFlagMask = CRITICAL_PACKET | URGENT_PACKET;
    if ((flags & ~allowedFlagMask) != 0) {
        RegisterIrregularity(match, idx, "flags de paquete no validos");
        return;
    }

    bool isCriticalPacket = (flags & CRITICAL_PACKET) != 0;
    bool isUrgentPacket = (flags & URGENT_PACKET) != 0;

    // En esta practica no tenemos acciones urgentes que no sean criticas.
    // Por tanto, URGENT solo es valido combinado con CRITICAL.
    if (isUrgentPacket && !isCriticalPacket) {
        RegisterIrregularity(match, idx, "paquete urgente sin ser critico");
        return;
    }

    // Los paquetes importantes se confirman con ACK para no repetirlos.
    // El disparo es critico porque necesita ACK y urgente porque se procesa con prioridad.
    if (isCriticalPacket) {
        // Los paquetes criticos/urgentes de este juego solo pueden representar un disparo
        if (in.packetID == 0 || in.inputMask != INPUT_SHOOT || in.movementID != 0) {
            RegisterIrregularity(match, idx, "paquete critico/urgente no valido");
            return;
        }

        // Si alguien manda un disparo critico sin URGENT, lo rechazamos para que la implementacion
        // quede coherente con la teoria de combinacion de flags.
        if (!isUrgentPacket) {
            RegisterIrregularity(match, idx, "disparo critico sin flag urgente");
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
                            match.worldState.bullets[i].posX = match.worldState.players[idx].posX + BULLET_SPAWN_OFFSET;
                            match.worldState.bullets[i].velX = BULLET_SPEED;
                        }
                        else {
                            match.worldState.bullets[i].posX = match.worldState.players[idx].posX - BULLET_SPAWN_OFFSET;
                            match.worldState.bullets[i].velX = -BULLET_SPEED;
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

        if (in.hasPredictedPosition) {
            ClientMovementSnapshot movement;
            movement.movementID = in.movementID;
            movement.timestamp = in.timestamp;
            movement.inputMask = in.inputMask;
            movement.posX = in.predictedPosX;
            movement.posY = in.predictedPosY;

            // Guardamos la posicion predicha, pero no se confirma hasta que ValidateMovements la revise.
            InsertMovementSnapshot(match, idx, movement);
        }
    }
}

void Server::InsertMovementSnapshot(MatchData& match, short playerIndex, const ClientMovementSnapshot& movement) {
    if (movement.movementID <= match.lastProcessedMovementID[playerIndex]) {
        return;
    }

    // Si acabamos de hacer BACK por respawn o correccion, pueden llegar paquetes antiguos por UDP.
    // Esos paquetes se ignoran sin irregularidad hasta que llegue una posicion cercana al BACK.
    if (match.movementCorrectionPending[playerIndex]) {
        short diffX = movement.posX - match.lastValidPosX[playerIndex];
        short diffY = movement.posY - match.lastValidPosY[playerIndex];

        bool nearBackPosition =
            std::abs((int)diffX) <= MOVEMENT_VALIDATION_TOLERANCE &&
            std::abs((int)diffY) <= MOVEMENT_VALIDATION_TOLERANCE;

        if (!nearBackPosition) {
            std::cout << "[RECONCILIACION] paquete antiguo ignorado tras BACK jugador "
                << match.worldState.players[playerIndex].id
                << " movementID " << movement.movementID << std::endl;
            return;
        }

        // El cliente ya ha vuelto a la posicion corregida y empieza a mandar movimientos nuevos.
        match.movementCorrectionPending[playerIndex] = false;

        if (match.worldState.movementBackRequired &&
            match.worldState.backPlayerID == match.worldState.players[playerIndex].id) {
            match.worldState.movementBackRequired = false;
            match.worldState.backPlayerID = 0;
            match.worldState.backMovementID = 0;
            match.worldState.backPosX = 0;
            match.worldState.backPosY = 0;
        }
    }

    for (std::vector<ClientMovementSnapshot>::iterator it = match.pendingMovements[playerIndex].begin();
        it != match.pendingMovements[playerIndex].end();
        ++it) {

        if (it->movementID == movement.movementID) {
            return;
        }

        if (it->movementID > movement.movementID) {
            match.pendingMovements[playerIndex].insert(it, movement);
            return;
        }
    }

    match.pendingMovements[playerIndex].push_back(movement);
}

bool Server::CheckGround(short x, short y, short w, short h) {
    return CheckTileCollision(x, y + 1, w, h);
}

void Server::ValidateMovements(MatchData& match) {
    if (match.worldState.isGameOver) return;

    for (short i = 0; i < 2; i++) {
        PlayerState& p = match.worldState.players[i];
        short mask = match.playerInputs[i];

        p.isTaunting = (mask & INPUT_TAUNT) != 0;

        if (mask & INPUT_LEFT) p.facingRight = false;
        if (mask & INPUT_RIGHT) p.facingRight = true;

        while (!match.pendingMovements[i].empty()) {
            ClientMovementSnapshot movement = match.pendingMovements[i].front();
            match.pendingMovements[i].erase(match.pendingMovements[i].begin());

            if (movement.movementID <= match.lastProcessedMovementID[i]) {
                continue;
            }

            // Si el servidor acaba de hacer BACK, los paquetes que sigan lejos de la posicion corregida son antiguos.
            // Se descartan sin irregularidad porque no son cheating, son paquetes UDP anteriores al respawn/correccion.
            if (match.movementCorrectionPending[i]) {
                short diffBackX = movement.posX - match.lastValidPosX[i];
                short diffBackY = movement.posY - match.lastValidPosY[i];

                bool nearBackPosition =
                    std::abs((int)diffBackX) <= MOVEMENT_VALIDATION_TOLERANCE &&
                    std::abs((int)diffBackY) <= MOVEMENT_VALIDATION_TOLERANCE;

                if (!nearBackPosition) {
                    match.pendingMovements[i].clear();
                    std::cout << "[RECONCILIACION] movimiento antiguo descartado tras BACK jugador "
                        << p.id << std::endl;
                    break;
                }

                match.movementCorrectionPending[i] = false;

                if (match.worldState.movementBackRequired &&
                    match.worldState.backPlayerID == p.id) {
                    match.worldState.movementBackRequired = false;
                    match.worldState.backPlayerID = 0;
                    match.worldState.backMovementID = 0;
                    match.worldState.backPosX = 0;
                    match.worldState.backPosY = 0;
                }
            }

            // Calculamos cuanto tiempo ha pasado desde el ultimo movimiento valido.
            int elapsedMs = MINIMUM_VALIDATION_TIME_TOLERANCE;

            if (match.lastValidMovementTimestamp[i] > 0 &&
                movement.timestamp > match.lastValidMovementTimestamp[i]) {
                elapsedMs = movement.timestamp - match.lastValidMovementTimestamp[i];
            }

            // Limitamos el rango para evitar validaciones raras por paquetes duplicados, perdidos o pausas grandes.
            if (elapsedMs < MINIMUM_VALIDATION_TIME_TOLERANCE) elapsedMs = MINIMUM_VALIDATION_TIME_TOLERANCE;
            if (elapsedMs > MAXIMUM_VALIDATION_TIME_TOLERANCE) elapsedMs = MAXIMUM_VALIDATION_TIME_TOLERANCE;

            float elapsedTicks = elapsedMs / (float)MINIMUM_VALIDATION_TIME_TOLERANCE;

            int diffX = movement.posX - match.lastValidPosX[i];
            int diffY = movement.posY - match.lastValidPosY[i];

            // Distancia maxima aceptada segun velocidad horizontal, movimiento vertical y tolerancia por red.
            int maxHorizontalDistance = (int)(5.0f * elapsedTicks) + MOVEMENT_VALIDATION_TOLERANCE;
            int maxVerticalDistance = (int)(MINIMUM_VALIDATION_TIME_TOLERANCE * elapsedTicks) + MOVEMENT_VALIDATION_TOLERANCE;

            bool distanceOk = std::abs(diffX) <= maxHorizontalDistance && std::abs(diffY) <= maxVerticalDistance;

            bool mapOk = !CheckTileCollision(movement.posX, movement.posY, PLAYER_HITBOX_W, PLAYER_HITBOX_H);

            if (!distanceOk || !mapOk) {
                SetBackCorrection(match, i, "movimiento BAD, vuelve al ultimo ID valido", true);
                std::cout << "DistanceOK:" << distanceOk << "-------- " << "MapOk: " << mapOk << std::endl;
                break;
            }

            short previousX = p.posX;
            short previousY = p.posY;

            // El servidor acepta la posicion predicha y la convierte en estado oficial.
            p.posX = movement.posX;
            p.posY = movement.posY;
            p.velX = p.posX - previousX;
            p.velY = p.posY - previousY;
            p.onGround = CheckGround(p.posX, p.posY, PLAYER_HITBOX_W, PLAYER_HITBOX_H);

            match.lastValidPosX[i] = p.posX;
            match.lastValidPosY[i] = p.posY;

            // Guardamos tambien el tiempo aceptado para validar el siguiente movimiento contra este punto.
            match.lastValidMovementTimestamp[i] = movement.timestamp;
            match.lastProcessedMovementID[i] = movement.movementID;
            match.worldState.players[i].lastProcessedMovementID = movement.movementID;

            if (p.posY > DEATH_Y_LIMIT) {
                short rivalID = (i == 0) ? match.worldState.players[1].id : match.worldState.players[0].id;
                HandlePlayerDeath(match, i, rivalID);
                break;
            }
        }
    }
}

// colision balas/mapa/jugadores/muerte
void Server::UpdatePhysics(MatchData& match) {
    if (match.worldState.isGameOver) return;

    // Validamos las posiciones predichas que han enviado los clientes.
    ValidateMovements(match);

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
        std::cout << "[UDP] mapa cargado. Dimensiones: " << mapCols << "x" << mapRows << std::endl;
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
        match.worldState.players[i].lastProcessedMovementID = (unsigned short)match.lastProcessedMovementID[i];

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
        match.worldState.players[playerIndex].onGround = CheckGround(RESPAWN_POS_X, RESPAWN_POS_Y, PLAYER_HITBOX_W, PLAYER_HITBOX_H);

        // El respawn es una posicion valida nueva del servidor.
        match.lastValidPosX[playerIndex] = RESPAWN_POS_X;
        match.lastValidPosY[playerIndex] = RESPAWN_POS_Y;
        match.playerInputs[playerIndex] = INPUT_NONE;
        match.pendingMovements[playerIndex].clear();

        // El respawn obliga a reconciliar con BACK, pero NO es una irregularidad.
        SetBackCorrection(match, playerIndex, "respawn despues de muerte", false);
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

void Server::SendPingPackets(MatchData& match) {
    if (match.worldState.isGameOver) return;

    // No enviamos Ping hasta que los dos jugadores esten conectados a la partida UDP.
    if (!match.clientConnected[0] || !match.clientConnected[1]) return;

    if (match.pingClock.getElapsedTime().asSeconds() < PING_INTERVAL_SECONDS) return;
    match.pingClock.restart();

    for (short i = 0; i < 2; i++) {
        if (!match.clientConnected[i] || !match.clientIPs[i].has_value()) continue;

        S2C_PingPacket ping;
        ping.matchID = match.worldState.matchID;
        ping.playerID = match.worldState.players[i].id;
        ping.pingID = match.nextPingID[i]++;
        ping.timestamp = match.serverClock.getElapsedTime().asMilliseconds();

        // Guardamos el timestamp del ultimo Ping enviado.
        // Cuando llegue el Pong, se calcula RTT con este tiempo.
        match.lastPingTimestamp[i] = ping.timestamp;

        udpSocket.send(&ping, sizeof(S2C_PingPacket), match.clientIPs[i].value(), match.clientPorts[i]);

        match.waitingPong[i] = true;

        std::cout << "[PING/PONG] PING enviado a jugador "
            << match.worldState.players[i].id
            << " pingID " << ping.pingID
            << " timestamp " << ping.timestamp << std::endl;
    }
}

void Server::RegisterPong(MatchData& match, short playerIndex, unsigned short pingID, unsigned int pingTimestamp) {
    if (playerIndex < 0 || playerIndex >= 2) return;
    if (match.worldState.isGameOver) return;

    // Si llega un Pong repetido o antiguo, se ignora.
    if (pingID <= match.lastPongID[playerIndex]) {
        return;
    }

    unsigned int currentServerTime = match.serverClock.getElapsedTime().asMilliseconds();

    // RTT:
    // El cliente devuelve el timestamp original del Ping.
    // Por tanto, el servidor calcula la latencia ida/vuelta como:
    // RTT = tiempoActualServidor - timestampPingOriginal
    float rtt = 0.0f;
    if (currentServerTime >= pingTimestamp) {
        rtt = (float)(currentServerTime - pingTimestamp);
    }

    match.lastRTT[playerIndex] = rtt;

    // Media suavizada para que no salte demasiado de un Ping a otro.
    if (!match.hasRTT[playerIndex]) {
        match.averageRTT[playerIndex] = rtt;
        match.hasRTT[playerIndex] = true;
    }
    else {
        match.averageRTT[playerIndex] =
            (match.averageRTT[playerIndex] * 0.8f) + (rtt * 0.2f);
    }

    match.lastPongID[playerIndex] = pingID;
    match.waitingPong[playerIndex] = false;
    match.pongClock[playerIndex].restart();
    match.timeoutWarningShown[playerIndex] = false;

    std::cout << "[PING/PONG] PONG recibido de jugador "
        << match.worldState.players[playerIndex].id
        << " pingID " << pingID
        << " RTT " << match.lastRTT[playerIndex] << " ms"
        << " | RTT medio " << match.averageRTT[playerIndex] << " ms" << std::endl;
}

void Server::CheckTimeouts(MatchData& match) {
    // Si la partida ya acabo no hace falta mirar desconexiones
    if (match.worldState.isGameOver) return;

    // Las desconexiones se comprueban cuando ya estan los dos jugadores
    if (!match.clientConnected[0] || !match.clientConnected[1]) return;

    for (short i = 0; i < 2; i++) {
        // PING/PONG:

        //jugado conectado si responde con PONG a los PING del servidor.
        float secondsWithoutPong = match.pongClock[i].getElapsedTime().asSeconds();

        // Sale un aviso por consola antes de echar al jugador
        if (secondsWithoutPong > PONG_WARNING_SECONDS && !match.timeoutWarningShown[i]) {
            match.timeoutWarningShown[i] = true;
            std::cout << "[PING/PONG] jugador "
                << match.worldState.players[i].id
                << " no responde PONG, avisando" << std::endl;
        }

        // Si el timeout se cumple el jugador pierde y gana el rival
        if (secondsWithoutPong > PONG_TIMEOUT_SECONDS) {
            short winnerIndex = (i == 0) ? 1 : 0;

            match.worldState.isGameOver = true;
            match.worldState.winnerID = match.worldState.players[winnerIndex].id;

            match.pendingDelete = true;
            match.endClock.restart();

            std::cout << "[PING/PONG] timeout: jugador "
                << match.worldState.players[i].id
                << " no ha respondido PONG. Gana jugador "
                << match.worldState.winnerID << std::endl;
            return;
        }
    }
}

void Server::Stop() {
    isRunning = false;
    matches.clear();
    udpSocket.unbind();
}