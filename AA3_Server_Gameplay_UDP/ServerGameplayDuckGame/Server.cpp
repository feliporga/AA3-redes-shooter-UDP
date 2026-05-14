#include "Server.h"
#include <cstring>
#include <thread>
#include <chrono>

Server::Server(unsigned short port) : port(port), isRunning(false) {
    // Inicializar variables con bucle clásico
    for (short i = 0; i < 2; i++) {
        clientConnected[i] = false;
        lastTimestamps[i] = 0;
        playerInputs[i] = INPUT_NONE;

        // Inicializar estado del mundo (Player 1 izq, Player 2 der)
        worldState.players[i].id = i + 1;
        worldState.players[i].posX = 100 + (i * 400);
        worldState.players[i].posY = 300;
        worldState.players[i].velX = 0;
        worldState.players[i].velY = 0;
        worldState.players[i].onGround = false;
        worldState.players[i].facingRight = (i == 0);
    }
}

Server::~Server() {
    Stop();
}

void Server::Run() {
    if (udpSocket.bind(port) != sf::Socket::Status::Done) {
        std::cout << "[UDP ERROR] No se pudo abrir el puerto " << port << std::endl;
        return;
    }
    udpSocket.setBlocking(false);
    isRunning = true;
    std::cout << "[SERVER UDP] Escuchando gameplay autoritativo en puerto " << port << std::endl;

    // Bucle de físicas a 60 Ticks por segundo
    const sf::Time timePerTick = sf::seconds(1.0f / 60.0f);
    sf::Clock tickClock;
    sf::Time timeSinceLastUpdate = sf::Time::Zero;

    while (isRunning) {
        timeSinceLastUpdate += tickClock.restart();

        while (timeSinceLastUpdate > timePerTick) {
            timeSinceLastUpdate -= timePerTick;

            ProcessPackets();
            UpdatePhysics();
            BroadcastState();
        }

        // Thread pool básico: Dormir el hilo 1ms para no poner la CPU al 100%
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Server::ProcessPackets() {
    char buffer[1024];
    std::size_t received;

    // SFML 3 exige optional para las IPs vacías que van a recibir datos
    std::optional<sf::IpAddress> sender;
    unsigned short senderPort;

    while (udpSocket.receive(buffer, sizeof(buffer), received, sender, senderPort) == sf::Socket::Status::Done) {
        short type;
        std::memcpy(&type, buffer, sizeof(short));

        if (type == (short)PacketType::GameMove) {
            C2S_InputPacket in;
            std::memcpy(&in, buffer, sizeof(C2S_InputPacket));

            short idx = in.playerID - 1;

            if (idx >= 0 && idx < 2) {
                if (!clientConnected[idx]) {
                    clientIPs[idx] = sender; // Guardamos el optional entero
                    clientPorts[idx] = senderPort;
                    clientConnected[idx] = true;
                    // Para imprimirlo, usamos ->toString() porque es un optional
                    std::cout << "[UDP] Jugador " << in.playerID << " enlazado desde " << sender->toString() << ":" << senderPort << std::endl;
                }

                if (in.timestamp >= lastTimestamps[idx]) {
                    lastTimestamps[idx] = in.timestamp;
                    playerInputs[idx] = in.inputMask;
                }
            }
        }
    }
}

void Server::UpdatePhysics() {
    for (short i = 0; i < 2; i++) {
        PlayerState& p = worldState.players[i];
        short mask = playerInputs[i];

        // 1. Gravedad
        p.velY += 1;

        // 2. Movimiento Horizontal (Desempaquetando bitmask)
        p.velX = 0;
        if (mask & INPUT_LEFT) {
            p.velX = -5;
            p.facingRight = false;
        }
        if (mask & INPUT_RIGHT) {
            p.velX = 5;
            p.facingRight = true;
        }

        // 3. Salto
        if ((mask & INPUT_JUMP) && p.onGround) {
            p.velY = -15;
            p.onGround = false;
        }

        // 4. Aplicar velocidades
        p.posX += p.velX;
        p.posY += p.velY;

        // 5. Colisión con suelo temporal (Hardcodeado a Y=500)
        if (p.posY >= 500) {
            p.posY = 500;
            p.velY = 0;
            p.onGround = true;
        }
    }
}

void Server::BroadcastState() {
    worldState.timestamp = serverClock.getElapsedTime().asMilliseconds();

    for (short i = 0; i < 2; i++) {
        // Asegurarnos de que el jugador está conectado y tiene una IP guardada
        if (clientConnected[i] && clientIPs[i].has_value()) {
            // SFML 3 exige sacar el valor real de la IP usando .value()
            udpSocket.send(&worldState, sizeof(S2C_StatePacket), clientIPs[i].value(), clientPorts[i]);
        }
    }
}

void Server::Stop() {
    isRunning = false;
    udpSocket.unbind();
}