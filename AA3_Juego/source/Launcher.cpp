#include "Launcher.h"

bool Launcher::Run() {
    std::cout << "======================================" << std::endl;
    std::cout << "      DUCK GAME - LAUNCHER V1.0       " << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "conectando con el servidor de parcheo..." << std::endl;

    sf::TcpSocket socket;
    if (socket.connect(sf::IpAddress::resolve(SERVER_TCP_IP).value(), SERVER_TCP_PORT, sf::seconds(5.0f)) != sf::Socket::Status::Done) {
        std::cout << "[ERROR] no se pudo conectar al servidor tcp" << std::endl;
        return false;
    }

    short localVersion = 0;
    // leemos la version que tenemos en la carpeta resources
    std::ifstream versionFile("resources/mapa_version.txt");
    if (versionFile.is_open()) {
        versionFile >> localVersion;
        versionFile.close();
    }

    std::cout << "version local del mapa: " << localVersion << std::endl;
    std::cout << "verificando actualizaciones..." << std::endl;

    sf::Packet req;
    req << (short)PacketType::CheckMapVersion << localVersion;
    socket.send(req);

    sf::Packet res;
    if (socket.receive(res) == sf::Socket::Status::Done) {
        short typeInt;
        res >> typeInt;

        if (typeInt == (short)PacketType::UpdateMap) {
            short newVersion;
            std::string mapData;
            res >> newVersion >> mapData;

            std::cout << "NUEVA VERSION ENCONTRADA: v" << newVersion << std::endl;
            std::cout << "descargando e instalando mapa..." << std::endl;

            // escribimos el nuevo mapa en la carpeta resources
            std::ofstream mapFile("resources/mapa.txt");
            mapFile << mapData;
            mapFile.close();

            // actualizamos el numerito de la version
            std::ofstream vFile("resources/mapa_version.txt");
            vFile << newVersion;
            vFile.close();

            std::cout << "actualizacion completada con exito" << std::endl;
        }
        else if (typeInt == (short)PacketType::MapUpToDate) {
            std::cout << "todos los archivos estan en la ultima version" << std::endl;
        }
    }

    socket.disconnect();
    std::cout << "lanzando el juego..." << std::endl;
    std::cout << "======================================" << std::endl;

    sf::sleep(sf::seconds(1.0f));
    return true;
}