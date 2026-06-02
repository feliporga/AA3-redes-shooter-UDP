#include "GameScene.h"
#include "NetworkManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include <iostream>
#include <fstream>

// Valores utilizados para la predicción local y la interpolación del rival
const float LOCAL_PREDICTION_SPEED = 5.0f;
const unsigned int INTERPOLATION_DELAY = 100;
const unsigned int MAX_REMOTE_SNAPSHOTS = 30;

class TileObject : public Object {
public:
    TileObject(float x, float y) : Object() {
        transform->position = Vector2(x, y);
        transform->size = Vector2(32, 32);
        renderer = new ImageRenderer(transform, "resources/tile.png", Vector2(0, 0), Vector2(32, 32));
    }
};

GameScene::GameScene() : Scene() {
    statusText = nullptr;
    lastServerTimestamp = 0;
    hasInitialServerState = false;
    nextMovementID = 1;
    hasServerTimeOffset = false;
    serverTimeOffset = 0;

    // Inicializar balas
    for (int i = 0; i < MAX_BULLETS; i++) {
        bulletActive[i] = false;
    }
}

GameScene::~GameScene() {
    for (short i = 0; i < 2; i++) {
        if (playerModels[i].rendIdle) delete playerModels[i].rendIdle;
        if (playerModels[i].rendWalk) delete playerModels[i].rendWalk;
        if (playerModels[i].rendJump) delete playerModels[i].rendJump;
    }
}

void GameScene::LoadMap() {
    std::ifstream file("resources/mapa.txt");
    if (!file.is_open()) return;

    std::string line;
    int row = 0;

    while (std::getline(file, line)) {
        int col = 0;

        // El mapa actual utiliza valores 0 y 1 separados por espacios
        for (std::size_t i = 0; i < line.size(); i++) {
            if (line[i] == '0' || line[i] == '1') {
                int tileType = line[i] - '0';

                if (tileType == 1) {
                    TileObject* tile = new TileObject(col * 32.0f, row * 32.0f);
                    SPAWN.SpawnObject(tile);
                }

                col++;
            }
        }

        row++;
    }
    file.close();
}

void GameScene::LoadPlayerAssets(short index, short playerID) {
    std::string character = (playerID == NM.myPlayerID) ? "Mario" : "Luigi";

    std::string pathIdle = "resources/Idle" + character + ".png";
    std::string pathWalk = "resources/Walk" + character + ".png";
    std::string pathJump = "resources/Jump" + character + ".png";

    playerModels[index].transform.position = Vector2(0, 0);
    playerModels[index].transform.scale = Vector2(1, 1);
    playerModels[index].transform.size = Vector2(32, 32);

    playerModels[index].rendIdle = new ImageRenderer(
        &playerModels[index].transform, pathIdle, Vector2(0, 0), Vector2(32, 32));

    playerModels[index].rendJump = new ImageRenderer(
        &playerModels[index].transform, pathJump, Vector2(0, 0), Vector2(32, 32));

    playerModels[index].rendWalk = new AnimatedImageRenderer(
        32, 32, 10, true, &playerModels[index].transform, pathWalk, Vector2(0, 0), Vector2(128, 32));

    playerModels[index].activeRenderer = playerModels[index].rendIdle;
}

void GameScene::ApplyLocalHorizontalPrediction(short inputMask) {
    if (!hasInitialServerState || NM.myPlayerID == 0) return;

    short localIndex = (NM.myPlayerID == 1) ? 0 : 1;
    float predictedVelocityX = 0.0f;

    if (inputMask & INPUT_LEFT) predictedVelocityX = -LOCAL_PREDICTION_SPEED;
    if (inputMask & INPUT_RIGHT) predictedVelocityX = LOCAL_PREDICTION_SPEED;

    playerModels[localIndex].transform.position.x += predictedVelocityX;
}

void GameScene::ReconcileLocalPlayer(const PlayerState& serverPlayer) {
    short localIndex = (NM.myPlayerID == 1) ? 0 : 1;

    // BACK:
    // Volvemos siempre a la posición oficial que ha calculado el servidor.
    playerModels[localIndex].transform.position =
        Vector2((float)serverPlayer.posX, (float)serverPlayer.posY);

    // Eliminamos los inputs que el servidor ya ha incluido en su posición oficial.
    for (std::size_t i = 0; i < pendingMovements.size(); ) {
        if (pendingMovements[i].movementID <= serverPlayer.lastProcessedMovementID) {
            pendingMovements.erase(pendingMovements.begin() + i);
        }
        else {
            i++;
        }
    }

    // Reaplicamos únicamente los inputs posteriores que aún no ha confirmado el servidor.
    for (std::size_t i = 0; i < pendingMovements.size(); i++) {
        ApplyLocalHorizontalPrediction(pendingMovements[i].inputMask);
    }
}

void GameScene::AddRemoteSnapshot(const PlayerState& serverPlayer, unsigned int timestamp) {
    RemoteSnapshot newSnapshot;
    newSnapshot.timestamp = timestamp;
    newSnapshot.position = Vector2((float)serverPlayer.posX, (float)serverPlayer.posY);

    bool inserted = false;

    // Guardamos los estados del rival ordenados para que los paquetes UDP desordenados
    // no provoquen que el personaje vuelva atrás en pantalla.
    for (std::vector<RemoteSnapshot>::iterator it = remoteSnapshots.begin(); it != remoteSnapshots.end(); ++it) {
        if (it->timestamp == newSnapshot.timestamp) {
            return;
        }

        if (it->timestamp > newSnapshot.timestamp) {
            remoteSnapshots.insert(it, newSnapshot);
            inserted = true;
            break;
        }
    }

    if (!inserted) {
        remoteSnapshots.push_back(newSnapshot);
    }

    if (remoteSnapshots.size() > MAX_REMOTE_SNAPSHOTS) {
        remoteSnapshots.erase(remoteSnapshots.begin());
    }
}

void GameScene::InterpolateRemotePlayer(unsigned int currentTime) {
    if (!hasInitialServerState || NM.myPlayerID == 0 || remoteSnapshots.empty()) return;

    short enemyIndex = (NM.myPlayerID == 1) ? 1 : 0;

    if (!hasServerTimeOffset) {
        serverTimeOffset = (int)currentTime - (int)remoteSnapshots.back().timestamp;
        hasServerTimeOffset = true;
    }

    // Dibujamos al rival unos milisegundos por detrás para tener dos estados
    // entre los que interpolar y evitar teletransportes.
    int renderTimestamp =
        (int)currentTime - serverTimeOffset - (int)INTERPOLATION_DELAY;

    while (remoteSnapshots.size() >= 2 &&
        (int)remoteSnapshots[1].timestamp <= renderTimestamp) {
        remoteSnapshots.erase(remoteSnapshots.begin());
    }

    if (remoteSnapshots.size() == 1) {
        playerModels[enemyIndex].transform.position = remoteSnapshots[0].position;
        return;
    }

    RemoteSnapshot& firstSnapshot = remoteSnapshots[0];
    RemoteSnapshot& secondSnapshot = remoteSnapshots[1];

    float timeRange = (float)(secondSnapshot.timestamp - firstSnapshot.timestamp);
    float interpolationValue = 0.0f;

    if (timeRange > 0.0f) {
        interpolationValue =
            ((float)renderTimestamp - (float)firstSnapshot.timestamp) / timeRange;
    }

    if (interpolationValue < 0.0f) interpolationValue = 0.0f;
    if (interpolationValue > 1.0f) interpolationValue = 1.0f;

    playerModels[enemyIndex].transform.position.x =
        firstSnapshot.position.x +
        (secondSnapshot.position.x - firstSnapshot.position.x) * interpolationValue;

    playerModels[enemyIndex].transform.position.y =
        firstSnapshot.position.y +
        (secondSnapshot.position.y - firstSnapshot.position.y) * interpolationValue;
}

void GameScene::OnEnter() {
    lastServerTimestamp = 0;
    hasInitialServerState = false;
    nextMovementID = 1;
    pendingMovements.clear();
    remoteSnapshots.clear();
    hasServerTimeOffset = false;
    serverTimeOffset = 0;
    gameClock.restart();

    LoadPlayerAssets(0, 1);
    LoadPlayerAssets(1, 2);

    LoadMap();

    statusText = new TextObject("CONECTADO AL SERVER UDP");
    statusText->GetTransform()->position = Vector2(50, 50);
    SPAWN.SpawnObject(statusText);
}

void GameScene::Update() {
    Scene::Update();
    unsigned int currentTime = gameClock.getElapsedTime().asMilliseconds();

    // INPUTS:

    if (!NM.lastState.isGameOver) {
        // MOVEMENT
        short mask = INPUT_NONE;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) mask |= INPUT_LEFT;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) mask |= INPUT_RIGHT;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::T)) mask |= INPUT_TAUNT;

        // JUMP
        static bool prevJump = false;
        static bool prevOnGround = true;

        bool currentJump = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);
        short myIndex = (NM.lastState.players[0].id == NM.myPlayerID) ? 0 : 1;
        bool currentOnGround = false;

        if (hasInitialServerState) {
            currentOnGround = NM.lastState.players[myIndex].onGround;
        }

        if (currentJump) {
            mask |= INPUT_JUMP;
            if ((!prevJump && currentOnGround) || (prevJump && !prevOnGround && currentOnGround)) {
                AM.PlayClip("marioJump", 0);
            }
        }

        prevJump = currentJump;
        prevOnGround = currentOnGround;

        unsigned int movementID = nextMovementID++;

        NM.SendUDPInput(mask, currentTime, movementID);

        // PREDICCIÓN LOCAL:
        // El jugador propio responde al movimiento horizontal sin esperar la respuesta del servidor.
        // El input queda almacenado hasta que el servidor confirme que ya lo ha procesado.
        if (hasInitialServerState && NM.myPlayerID != 0) {
            PendingMovement newMovement;
            newMovement.movementID = movementID;
            newMovement.inputMask = mask;
            pendingMovements.push_back(newMovement);

            ApplyLocalHorizontalPrediction(mask);
        }

        // SHOUT (F)
        static bool prevShoot = false;
        bool currentShoot = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F);
        if (currentShoot && !prevShoot) {
            NM.SendCriticalUDPInput(INPUT_SHOOT, currentTime);
            AM.PlayClip("SuperMarioBrosFireball", 0);
        }
        prevShoot = currentShoot;
    }
    // PROCESAR RED
    NM.RetryUnackedPackets(currentTime);
    NM.ListenUDP();

    // ACTUALIZAR TRANSFORMS CON DATOS DEL SERVIDOR
    if (NM.newStateReceived) {
        if (NM.lastState.timestamp >= lastServerTimestamp) {
            lastServerTimestamp = NM.lastState.timestamp;

            // Taunt
            static bool prevTaunting[2] = { false, false };
            bool firstServerState = !hasInitialServerState;

            for (short i = 0; i < 2; i++) {
                PlayerState& s = NM.lastState.players[i];
                PlayerModel& m = playerModels[i];

                if (s.isTaunting && !prevTaunting[i]) {
                    std::cout << "[AUDIO] Jugador " << s.id << " hace la burla!" << std::endl;
                    AM.PlayClip("mariopapipo", 0);
                }
                prevTaunting[i] = s.isTaunting;

                bool isLocalPlayer = (s.id == NM.myPlayerID);

                if (firstServerState) {
                    // El primer estado recibido coloca directamente a ambos jugadores
                    m.transform.position = Vector2((float)s.posX, (float)s.posY);
                }
                else if (isLocalPlayer) {
                    // RECONCILIACIÓN:
                    // El servidor devuelve su posición oficial y el último movementID procesado.
                    // El cliente vuelve a ese punto y reaplica los movimientos aún pendientes.
                    ReconcileLocalPlayer(s);
                }

                if (!isLocalPlayer) {
                    // INTERPOLACIÓN:
                    // El rival se guarda en un buffer ordenado y se mostrará suavizado.
                    AddRemoteSnapshot(s, NM.lastState.timestamp);
                }

                if (s.velX < 0) m.transform.scale = Vector2(1.0f, 1.0f);
                else if (s.velX > 0) m.transform.scale = Vector2(-1.0f, 1.0f);

                if (!s.onGround) {
                    m.activeRenderer = m.rendJump;
                }
                else if (s.velX != 0) {
                    m.activeRenderer = m.rendWalk;
                }
                else {
                    m.activeRenderer = m.rendIdle;
                }

            }

            if (firstServerState) {
                // Los movimientos anteriores al primer estado ya pertenecen al arranque oficial
                pendingMovements.clear();
            }

            hasInitialServerState = true;

            for (int i = 0; i < MAX_BULLETS; i++) {
                bulletActive[i] = NM.lastState.bullets[i].active;
                bulletTransforms[i].position = Vector2((float)NM.lastState.bullets[i].posX, (float)NM.lastState.bullets[i].posY);
            }
        }
        NM.newStateReceived = false;
    }

    // INTERPOLACIÓN DEL RIVAL:
    // El rival se renderiza entre estados almacenados, no directamente en la última posición recibida.
    InterpolateRemotePlayer(currentTime);

    for (short i = 0; i < 2; i++) {
        if (playerModels[i].activeRenderer) {
            playerModels[i].activeRenderer->Update();
        }
    }

    // hud de vidas y win/lose
    if (NM.lastState.isGameOver) {
        if (NM.lastState.winnerID == NM.myPlayerID) {
            statusText->SetText("¡VICTORIA! Has ganado la partida.");
            statusText->SetColor(sf::Color::Green);
        }
        else {
            statusText->SetText("¡DERROTA! Te has quedado sin vidas.");
            statusText->SetColor(sf::Color::Red);
        }
    }
    else {
        short myIndex = (NM.lastState.players[0].id == NM.myPlayerID) ? 0 : 1;
        short enemyIndex = (myIndex == 0) ? 1 : 0;

        std::string hpInfo = "Mis Vidas: " + std::to_string(NM.lastState.players[myIndex].lives) +
            " | Mi HP: " + std::to_string(NM.lastState.players[myIndex].hp) +
            " --- HP Enemigo: " + std::to_string(NM.lastState.players[enemyIndex].hp);
        statusText->SetText(hpInfo);
    }
}

void GameScene::Render() {
    Scene::Render();

    for (short i = 0; i < 2; i++) {
        if (playerModels[i].activeRenderer) {
            playerModels[i].activeRenderer->Render();
        }
    }

    // bullet rendering
    sf::RenderWindow* window = RM->GetWindow();
    if (window) {
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bulletActive[i]) {
                sf::RectangleShape rect(sf::Vector2f(10.f, 10.f));
                rect.setFillColor(sf::Color::Yellow);
                rect.setPosition({ bulletTransforms[i].position.x, bulletTransforms[i].position.y });
                window->draw(rect);
            }
        }
    }
}

void GameScene::OnExit() {
    Scene::OnExit();
}