#include "GameScene.h"
#include "NetworkManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include "SceneManager.h"
#include <iostream>
#include <fstream>
// El resultado se queda unos segundos en pantalla antes de volver al menu
#define RETURN_TO_MENU_DELAY_SECONDS 3

#define CELL_SIZE 32
// Estos valores controlan la prediccion local y el movimiento suave del rival
const float LOCAL_PREDICTION_SPEED = 5.0f;
const unsigned short INTERPOLATION_DELAY = 100;
const unsigned short MAX_REMOTE_SNAPSHOTS = 30;


class TileObject : public Object {
public:
    TileObject(float x, float y) : Object() {
        transform->position = Vector2(x, y);
        transform->size = Vector2(CELL_SIZE, CELL_SIZE);
        renderer = new ImageRenderer(transform, "resources/tile.png", Vector2(0, 0), Vector2(CELL_SIZE, CELL_SIZE));
    }
};

GameScene::GameScene() : Scene() {
    statusText = nullptr;
    lastServerTimestamp = 0;
    hasInitialServerState = false;
    nextMovementID = 1;
    hasServerTimeOffset = false;
    serverTimeOffset = 0;

    // Los avisos del anti-cheat empiezan ocultos hasta que el servidor detecta una irregularidad
    showIrregularityMessage = false;
    lastIrregularityEventID = 0;
    irregularityMessage = "";

    // La prediccion local empieza sin velocidad hasta recibir la primera posicion oficial
    localPredictedVelX = 0;
    localPredictedVelY = 0;
    localPredictedOnGround = false;
    localMapRows = 0;
    localMapCols = 0;

    // Inicializar balas
    for (short i = 0; i < MAX_BULLETS; i++) {
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

    localLevelMap.clear();
    localMapRows = 0;
    localMapCols = 0;

    std::string line;
    short row = 0;

    while (std::getline(file, line)) {
        short col = 0;
        std::vector<short> collisionRow;

        // El mapa actual utiliza valores 0 y 1 separados por espacios
        for (std::size_t i = 0; i < line.size(); i++) {
            if (line[i] == '0' || line[i] == '1') {
                short tileType = line[i] - '0';
                collisionRow.push_back(tileType);

                if (tileType == 1) {
                    TileObject* tile = new TileObject(col * CELL_SIZE, row * CELL_SIZE);
                    SPAWN.SpawnObject(tile);
                }

                col++;
            }
        }

        if (!collisionRow.empty()) {
            localLevelMap.push_back(collisionRow);
        }

        row++;
    }
    file.close();

    localMapRows = (short)localLevelMap.size();
    if (localMapRows > 0) {
        localMapCols = (short)localLevelMap[0].size();
    }
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

bool GameScene::CheckLocalTileCollision(short x, short y, short w, short h) {
    if (localMapRows == 0 || localMapCols == 0) return false;

    short TILE_SIZE = 32;
    short leftTile = x / TILE_SIZE;
    short rightTile = (x + w - 1) / TILE_SIZE;
    short topTile = y / TILE_SIZE;
    short bottomTile = (y + h - 1) / TILE_SIZE;

    // topes de seguridad
    if (leftTile < 0) leftTile = 0;
    if (rightTile >= localMapCols) rightTile = localMapCols - 1;
    if (topTile < 0) topTile = 0;
    if (bottomTile >= localMapRows) bottomTile = localMapRows - 1;

    for (short i = topTile; i <= bottomTile; i++) {
        for (short j = leftTile; j <= rightTile; j++) {
            if (localLevelMap[i][j] == 1) {
                return true;
            }
        }
    }

    return false;
}

bool GameScene::CheckLocalGround(short x, short y, short w, short h) {
    return CheckLocalTileCollision(x, y + 1, w, h);
}

void GameScene::InitializeLocalPredictionFromServer(const PlayerState& serverPlayer) {
    localPredictedVelX = serverPlayer.velX;
    localPredictedVelY = serverPlayer.velY;
    localPredictedOnGround = serverPlayer.onGround;
}

// Misma simulacion que usa el cliente para predecir y reconciliar: gravedad, input y colisiones.
void GameScene::SimulateLocalMovement(short inputMask, Transform& transform, short& velX, short& velY, bool& onGround) {
    short oldX = (short)transform.position.x;
    short oldY = (short)transform.position.y;

    velY += 1; // gravedad

    // El input horizontal se convierte en velocidad local antes de recibir confirmacion del servidor.
    bool movingLeft = (inputMask & INPUT_LEFT) != 0;
    bool movingRight = (inputMask & INPUT_RIGHT) != 0;

    if (movingLeft && !movingRight) velX = -(short)LOCAL_PREDICTION_SPEED;
    else if (movingRight && !movingLeft) velX = (short)LOCAL_PREDICTION_SPEED;
    else velX = 0;

    // El salto solo se acepta localmente si la ultima simulacion cree que estamos en el suelo.
    if ((inputMask & INPUT_JUMP) && onGround) {
        velY = PLAYER_JUMP_SPEED;
        onGround = false;
    }

    transform.position.x += velX;

    // Primero se resuelve el eje X para que la prediccion choque parecido al servidor.
    if (CheckLocalTileCollision((short)transform.position.x, (short)transform.position.y, PLAYER_HITBOX_W, PLAYER_HITBOX_H)) {
        transform.position.x = (float)oldX;
        velX = 0;
    }

    onGround = false;
    transform.position.y += velY;

    // Despues se resuelve el eje Y; si caemos contra el suelo marcamos onGround.
    if (CheckLocalTileCollision((short)transform.position.x, (short)transform.position.y, PLAYER_HITBOX_W, PLAYER_HITBOX_H)) {
        transform.position.y = (float)oldY;

        if (velY > 0) {
            onGround = true;
        }

        velY = 0;
    }
    else {
        onGround = CheckLocalGround((short)transform.position.x, (short)transform.position.y, PLAYER_HITBOX_W, PLAYER_HITBOX_H);
    }
}

void GameScene::ApplyLocalPrediction(short inputMask) {
    // Prediccion local: el jugador se mueve inmediatamente sin esperar al siguiente snapshot del servidor.
    if (!hasInitialServerState || NM.myPlayerID == 0) return;

    short localIndex = (NM.myPlayerID == 1) ? 0 : 1;
    SimulateLocalMovement(
        inputMask,
        playerModels[localIndex].transform,
        localPredictedVelX,
        localPredictedVelY,
        localPredictedOnGround);
}

void GameScene::ReconcileLocalPlayer(const PlayerState& serverPlayer) {
    short localIndex = (NM.myPlayerID == 1) ? 0 : 1;

    bool mustGoBack = NM.lastState.movementBackRequired &&
        NM.lastState.backPlayerID == NM.myPlayerID;

    if (mustGoBack) {
        // BACK:
        // El servidor ha detectado una posicion mala o un respawn y nos obliga a volver al ultimo punto valido.
        playerModels[localIndex].transform.position =
            Vector2((float)NM.lastState.backPosX, (float)NM.lastState.backPosY);

        localPredictedVelX = 0;
        localPredictedVelY = 0;
        localPredictedOnGround = serverPlayer.onGround;

        // Como el servidor dice BACK, cancelamos los movimientos posteriores almacenados.
        pendingMovements.clear();

        // Despues de un BACK, evitamos reutilizar IDs antiguos.
        if (nextMovementID <= NM.lastState.backMovementID) {
            nextMovementID = NM.lastState.backMovementID + 1;
        }

        std::cout << "[RECONCILIACION] BACK/" << NM.lastState.backMovementID
            << " -> (" << NM.lastState.backPosX << ", " << NM.lastState.backPosY << ")" << std::endl;
        return;
    }

    // Reconciliacion normal:
    // volvemos al estado autoritativo del servidor, eliminamos los inputs ya confirmados
    // y re-simulamos los inputs que todavia estan pendientes.
    for (std::size_t i = 0; i < pendingMovements.size(); ) {
        if (pendingMovements[i].movementID <= serverPlayer.lastProcessedMovementID) {
            pendingMovements.erase(pendingMovements.begin() + i);
        }
        else {
            i++;
        }
    }

    // Punto de partida autoritativo: el servidor manda la posicion/velocidad hasta el ultimo input validado.
    playerModels[localIndex].transform.position = Vector2((float)serverPlayer.posX, (float)serverPlayer.posY);
    localPredictedVelX = serverPlayer.velX;
    localPredictedVelY = serverPlayer.velY;
    localPredictedOnGround = serverPlayer.onGround;

    // Re-aplicamos los inputs que el cliente ya habia predicho pero el servidor aun no ha confirmado.
    for (std::size_t i = 0; i < pendingMovements.size(); i++) {
        SimulateLocalMovement(
            pendingMovements[i].inputMask,
            playerModels[localIndex].transform,
            localPredictedVelX,
            localPredictedVelY,
            localPredictedOnGround);

        // Actualizamos la posicion predicha asociada al input por si se vuelve a validar desde este estado corregido.
        pendingMovements[i].posX = (short)playerModels[localIndex].transform.position.x;
        pendingMovements[i].posY = (short)playerModels[localIndex].transform.position.y;
    }
}

void GameScene::AddRemoteSnapshot(const PlayerState& serverPlayer, unsigned int timestamp) {
    RemoteSnapshot newSnapshot;
    newSnapshot.timestamp = timestamp;
    newSnapshot.position = Vector2((float)serverPlayer.posX, (float)serverPlayer.posY);
    newSnapshot.movementID = serverPlayer.lastProcessedMovementID;

    bool inserted = false;

    // Los estados del rival se guardan ordenados aunque los paquetes UDP lleguen desordenados
    // no provoquen que el personaje vuelva atrás en pantalla.
    for (std::vector<RemoteSnapshot>::iterator it = remoteSnapshots.begin(); it != remoteSnapshots.end(); ++it) {
        if (it->movementID == newSnapshot.movementID && it->timestamp == newSnapshot.timestamp) {
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

    // El rival se dibuja con un pequeño retraso para poder suavizar el movimiento
    // entre los que interpolar y evitar teletransportes
    int renderTimestamp =
        (int)currentTime - serverTimeOffset - (int)INTERPOLATION_DELAY;

    while (remoteSnapshots.size() >= 2 &&
        (int)remoteSnapshots[1].timestamp <= renderTimestamp) {
        remoteSnapshots.erase(remoteSnapshots.begin());
    }

    if (remoteSnapshots.size() == 1) {
        // Si solo hay un snapshot, esperamos a que entre en la ventana de render para evitar un salto inicial.
        if ((int)remoteSnapshots[0].timestamp <= renderTimestamp) {
            playerModels[enemyIndex].transform.position = remoteSnapshots[0].position;
        }
        return;
    }

    RemoteSnapshot& firstSnapshot = remoteSnapshots[0];
    RemoteSnapshot& secondSnapshot = remoteSnapshots[1];

    float timeRange = (float)((int)secondSnapshot.timestamp - (int)firstSnapshot.timestamp);
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
    // El temporizador se prepara al entrar en una partida
    returnToMenuTimerStarted = false;
    lastServerTimestamp = 0;
    hasInitialServerState = false;
    nextMovementID = 1;
    pendingMovements.clear();
    remoteSnapshots.clear();
    hasServerTimeOffset = false;
    serverTimeOffset = 0;

    // Se limpian los avisos del anti-cheat al iniciar una partida nueva
    showIrregularityMessage = false;
    lastIrregularityEventID = 0;
    irregularityMessage = "";

    localPredictedVelX = 0;
    localPredictedVelY = 0;
    localPredictedOnGround = false;

    gameClock.restart();
    returnToMenuClock.restart();
    irregularityMessageClock.restart();

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

    // Se leen las teclas del jugador

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
        bool currentOnGround = localPredictedOnGround;

        if (currentJump) {
            mask |= INPUT_JUMP;
            if ((!prevJump && currentOnGround) || (prevJump && !prevOnGround && currentOnGround)) {
                AM.PlayClip("marioJump", 0);
            }
        }

        prevJump = currentJump;
        prevOnGround = currentOnGround;

        unsigned short movementID = nextMovementID++;

        bool hasPredictedPosition = false;
        short predictedPosX = 0;
        short predictedPosY = 0;

        // Se aplica prediccion local
        // El jugador se mueve en local sin esperar al servidor
        // El input se guarda hasta que el servidor lo confirme
        if (hasInitialServerState && NM.myPlayerID != 0) {
            ApplyLocalPrediction(mask);

            short localIndex = (NM.myPlayerID == 1) ? 0 : 1;
            predictedPosX = (short)playerModels[localIndex].transform.position.x;
            predictedPosY = (short)playerModels[localIndex].transform.position.y;
            hasPredictedPosition = true;

            PendingMovement newMovement;
            newMovement.movementID = movementID;
            newMovement.inputMask = mask;
            newMovement.posX = predictedPosX;
            newMovement.posY = predictedPosY;
            pendingMovements.push_back(newMovement);
        }

        NM.SendUDPInput(mask, currentTime, movementID, hasPredictedPosition, predictedPosX, predictedPosY);

        // SHOOT (F)
        static bool prevShoot = false;
        bool currentShoot = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F);
        if (currentShoot && !prevShoot) {
            NM.SendCriticalUDPInput(INPUT_SHOOT, currentTime);
            AM.PlayClip("SuperMarioBrosFireball", 0);
        }
        prevShoot = currentShoot;
    }
    // Se procesan los paquetes de red
    NM.RetryUnackedPackets(currentTime);
    NM.ListenUDP();

    // La escena se actualiza con los datos que manda el servidor
    if (NM.newStateReceived) {
        // Procesamos todos los snapshots acumulados, no solo el ultimo recibido. Asi el buffer de interpolacion del rival tiene mas puntos y evita saltos visuales.
        for (std::size_t stateIndex = 0; stateIndex < NM.pendingStates.size(); stateIndex++) {
            NM.lastState = NM.pendingStates[stateIndex];

            // Si UDP entrega un snapshot antiguo, no lo usamos para pisar el estado actual.
            if (NM.lastState.timestamp < lastServerTimestamp) {
                continue;
            }

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

                    if (isLocalPlayer) {
                        InitializeLocalPredictionFromServer(s);
                    }
                }
                else if (isLocalPlayer) {
                    // Reconciliacion:
                    // Si el servidor manda BACK volvemos a su posicion corregida.
                    // Si no manda BACK solo limpiamos los movimientos que ya ha validado.
                    ReconcileLocalPlayer(s);
                }

                if (!isLocalPlayer) {
                    // Se suaviza el movimiento del rival
                    // El rival se guarda ordenado para que se vea mas suave
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

            // El aviso del anti-cheat solo se procesa una vez por cada irregularidad detectada
            if (NM.lastState.correctionRequired &&
                NM.lastState.irregularityEventID != lastIrregularityEventID) {
                lastIrregularityEventID = NM.lastState.irregularityEventID;

                if (NM.lastState.irregularPlayerID == NM.myPlayerID) {
                    // La posicion local ya queda reconciliada con el estado oficial del servidor
                    irregularityMessage = "AVISO: irregularidad " +
                        std::to_string(NM.lastState.irregularityCount) +
                        "/3. Reconciliando.";
                }
                else {
                    irregularityMessage = "Rival advertido: irregularidad " +
                        std::to_string(NM.lastState.irregularityCount) + "/3.";
                }

                showIrregularityMessage = true;
                irregularityMessageClock.restart();
            }

            if (NM.lastState.movementBackRequired && NM.lastState.backPlayerID == NM.myPlayerID) {
                irregularityMessage = "BACK/" + std::to_string(NM.lastState.backMovementID) +
                    " -> posicion corregida por servidor.";
                showIrregularityMessage = true;
                irregularityMessageClock.restart();
            }

            for (short i = 0; i < MAX_BULLETS; i++) {
                bulletActive[i] = NM.lastState.bullets[i].active;
                bulletTransforms[i].position = Vector2((float)NM.lastState.bullets[i].posX, (float)NM.lastState.bullets[i].posY);
            }
        }
        // Una vez procesada la cola, la vaciamos para esperar los siguientes snapshots.
        NM.pendingStates.clear();
        NM.newStateReceived = false;
    }

    // Se suaviza el movimiento del rival, se dibuja entre estados guardados en vez de saltar a la ultima posicion
    InterpolateRemotePlayer(currentTime);

    for (short i = 0; i < 2; i++) {
        if (playerModels[i].activeRenderer) {
            playerModels[i].activeRenderer->Update();
        }
    }

    // hud de vidas y win/lose
    if (NM.lastState.isGameOver) {
        // Permite diferenciar si el jugador ha perdido por tres irregularidades
        bool lostByIrregularities = NM.lastState.gameOverByIrregularities &&
            NM.lastState.irregularPlayerID == NM.myPlayerID;

        if (!NM.resultReported) {
            // El resultado se envia una sola vez al servidor TCP
            bool hasWon = NM.lastState.winnerID == NM.myPlayerID;
            NM.SendReportResult(hasWon);
            NM.resultReported = true;

        }

        if (!returnToMenuTimerStarted) {
            // La espera empieza cuando acaba la partida
            returnToMenuTimerStarted = true;
            returnToMenuClock.restart();
        }

        if (NM.lastState.winnerID == NM.myPlayerID) {
            statusText->SetText("¡VICTORIA! Has ganado la partida.");
            statusText->SetColor(sf::Color::Green);
        }

        else if (lostByIrregularities) {
            statusText->SetText("¡DERROTA POR IRREGULARIDADES!");
            statusText->SetColor(sf::Color::Red);
        }

        else {
            statusText->SetText("¡DERROTA! Te has quedado sin vidas.");
            statusText->SetColor(sf::Color::Red);
        }
        // La cuenta atras evita volver al menu de golpe
        float remainingTime = RETURN_TO_MENU_DELAY_SECONDS - returnToMenuClock.getElapsedTime().asSeconds();

        if (remainingTime < 0.0f) {
            remainingTime = 0.0f;
        }

        if (NM.lastState.winnerID == NM.myPlayerID) {
            statusText->SetText("¡VICTORIA! Volviendo al menu en " + std::to_string((int)remainingTime + 1) + "...");
            statusText->SetColor(sf::Color::Green);
        }
        else if (lostByIrregularities) {
            statusText->SetText("¡DERROTA POR IRREGULARIDADES! Volviendo al menu en " + std::to_string((int)remainingTime + 1) + "...");
            statusText->SetColor(sf::Color::Red);
        }
        else {
            statusText->SetText("¡DERROTA! Volviendo al menu en " + std::to_string((int)remainingTime + 1) + "...");
            statusText->SetColor(sf::Color::Red);
        }

        if (returnToMenuClock.getElapsedTime().asSeconds() >= RETURN_TO_MENU_DELAY_SECONDS) {
            // Al terminar la espera se vuelve al menu
            SM.SetNextScene("MainMenu");
        }
    }
    else {
        short myIndex = (NM.lastState.players[0].id == NM.myPlayerID) ? 0 : 1;
        short enemyIndex = (myIndex == 0) ? 1 : 0;

        std::string hpInfo = "Mis Vidas: " + std::to_string(NM.lastState.players[myIndex].lives) +
            " | Mi HP: " + std::to_string(NM.lastState.players[myIndex].hp) +
            " --- HP Enemigo: " + std::to_string(NM.lastState.players[enemyIndex].hp);
        statusText->SetText(hpInfo);

        // La notificacion se mantiene unos segundos y despues vuelve a mostrar solamente el HUD
        if (showIrregularityMessage) {
            if (irregularityMessageClock.getElapsedTime().asSeconds() < 3.0f) {
                statusText->SetText(irregularityMessage + " | " + hpInfo);
            }
            else {
                showIrregularityMessage = false;
            }
        }
    }
}

void GameScene::Render() {
    Scene::Render();

    for (short i = 0; i < 2; i++) {
        if (playerModels[i].activeRenderer) {
            playerModels[i].activeRenderer->Render();
        }
    }

    // Se dibujan las balas activas
    sf::RenderWindow* window = RM->GetWindow();
    if (window) {
        for (short i = 0; i < MAX_BULLETS; i++) {
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