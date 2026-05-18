#include "Server.h"
#include <cstring>
#include <thread>
#include <chrono>

Server::Server(unsigned short port) : port(port), isRunning(false) {
    for (short i = 0; i < 2; i++) {
        clientConnected[i] = false;
        lastTimestamps[i] = 0;
        playerInputs[i] = INPUT_NONE;

        worldState.players[i].id = i + 1;
        worldState.players[i].posX = 100 + (i * 400);
        worldState.players[i].posY = 300;
        worldState.players[i].velX = 0;
        worldState.players[i].velY = 0;
        worldState.players[i].onGround = false;
        worldState.players[i].facingRight = (i == 0);
    }
}

Server::~Server() { Stop(); }

void Server::Run() {
    if (udpSocket.bind(port) != sf::Socket::Status::Done) {
        std::cout << "[UDP ERROR] No se pudo abrir el puerto " << port << std::endl;
        return;
    }
    udpSocket.setBlocking(false);
    isRunning = true;
    std::cout << "[SERVER UDP] Escuchando gameplay en puerto " << port << std::endl;

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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Server::ProcessPackets() {
    char buffer[1024];
    std::size_t received;
    std::optional<sf::IpAddress> sender;
    unsigned short senderPort;

    while (udpSocket.receive(buffer, sizeof(buffer), received, sender, senderPort) == sf::Socket::Status::Done) {
        uint8_t flags = buffer[0];

        // Leer siempre como si fuera un C2S_InputPacket base para sacar el ID
        C2S_InputPacket in;
        std::memcpy(&in, buffer, sizeof(C2S_InputPacket));

        // IDENTIFICACIÓN: BITMASK
        if (flags & CRITICAL_PACKET) {
            std::cout << "[UDP] Paquete CRITICO recibido (ID: " << in.packetID << "). Enviando ACK." << std::endl;
            S2C_AckPacket ack;
            ack.ackedPacketID = in.packetID;
            udpSocket.send(&ack, sizeof(S2C_AckPacket), sender.value(), senderPort);
        }

        if (in.type == (short)PacketType::GameMove) {
            short idx = in.playerID - 1;
            if (idx >= 0 && idx < 2) {
                if (!clientConnected[idx]) {
                    clientIPs[idx] = sender;
                    clientPorts[idx] = senderPort;
                    clientConnected[idx] = true;
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

        p.velY += 1; // Gravedad
        p.velX = 0;

        if (mask & INPUT_LEFT) { p.velX = -5; p.facingRight = false; }
        if (mask & INPUT_RIGHT) { p.velX = 5; p.facingRight = true; }
        if ((mask & INPUT_JUMP) && p.onGround) { p.velY = -15; p.onGround = false; }

        p.posX += p.velX;
        p.posY += p.velY;

        if (p.posY >= 500) { p.posY = 500; p.velY = 0; p.onGround = true; }
    }
}

void Server::BroadcastState() {
    worldState.timestamp = serverClock.getElapsedTime().asMilliseconds();
    for (short i = 0; i < 2; i++) {
        if (clientConnected[i] && clientIPs[i].has_value()) {
            udpSocket.send(&worldState, sizeof(S2C_StatePacket), clientIPs[i].value(), clientPorts[i]);
        }
    }
}

void Server::Stop() {
    isRunning = false;
    udpSocket.unbind();
}