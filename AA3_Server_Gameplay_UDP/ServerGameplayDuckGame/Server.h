#pragma once
#include <SFML/Network.hpp>
#include <SFML/System.hpp>
#include <iostream>
#include <optional>
#include "NetworkProtocol.h" // Asegúrate de tener este archivo copiado en esta solución

class Server {
private:
    unsigned short port;
    bool isRunning;
    sf::UdpSocket udpSocket;

    // Datos de la sala 1v1 (Arrays clásicos en lugar de vectores para optimizar RAM)
    std::optional<sf::IpAddress> clientIPs[2]; // <--- CAMBIA ESTO
    unsigned short clientPorts[2];
    bool clientConnected[2];

    unsigned int lastTimestamps[2];
    short playerInputs[2];

    S2C_StatePacket worldState;
    sf::Clock serverClock;

    // Métodos divididos correctamente
    void ProcessPackets();
    void UpdatePhysics();
    void BroadcastState();

public:
    Server(unsigned short port);
    ~Server();

    void Run();
    void Stop();
};