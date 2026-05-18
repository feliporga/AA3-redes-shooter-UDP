#include "GameScene.h"
#include "NetworkManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include <iostream>

GameScene::GameScene() : Scene() {
    statusText = nullptr;
    lastServerTimestamp = 0;
}

GameScene::~GameScene() {
    // Limpiamos los renderers usando tus destructores
    for (short i = 0; i < 2; i++) {
        if (playerModels[i].rendIdle) delete playerModels[i].rendIdle;
        if (playerModels[i].rendWalk) delete playerModels[i].rendWalk;
        if (playerModels[i].rendJump) delete playerModels[i].rendJump;
    }
}

void GameScene::LoadPlayerAssets(short index, short playerID) {
    std::string character = (playerID == 1) ? "Mario" : "Luigi";

    std::string pathIdle = "resources/Idle" + character + ".png";
    std::string pathWalk = "resources/Walk" + character + ".png";
    std::string pathJump = "resources/Jump" + character + ".png";

    // 1. Configuramos el Transform compartido para este jugador
    // Tu ImageRenderer ya usa transform->size para centrar el offset automáticamente
    playerModels[index].transform.position = Vector2(0, 0);
    playerModels[index].transform.scale = Vector2(1, 1);
    playerModels[index].transform.size = Vector2(32, 32);

    // 2. Creamos los Renderers usando tu Game Engine
    playerModels[index].rendIdle = new ImageRenderer(
        &playerModels[index].transform, pathIdle, Vector2(0, 0), Vector2(32, 32));

    playerModels[index].rendJump = new ImageRenderer(
        &playerModels[index].transform, pathJump, Vector2(0, 0), Vector2(32, 32));

    // Para la animación de caminar: 32x32, 10 FPS, Looping. 
    // SourceSize es 128x32 (porque tiene 4 frames de 32 de ancho)
    playerModels[index].rendWalk = new AnimatedImageRenderer(
        32, 32, 10, true, &playerModels[index].transform, pathWalk, Vector2(0, 0), Vector2(128, 32));

    // Empezamos en Idle
    playerModels[index].activeRenderer = playerModels[index].rendIdle;
}

void GameScene::OnEnter() {
    LoadPlayerAssets(0, 1);
    LoadPlayerAssets(1, 2);

    statusText = new TextObject("CONECTADO AL SERVER UDP");
    statusText->GetTransform()->position = Vector2(50, 50);
    SPAWN.SpawnObject(statusText);
}

void GameScene::Update() {
    Scene::Update();
    unsigned int currentTime = gameClock.getElapsedTime().asMilliseconds();

    // 1. INPUTS
    short mask = INPUT_NONE;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) mask |= INPUT_LEFT;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) mask |= INPUT_RIGHT;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space)) mask |= INPUT_JUMP; // Salto en el espacio

    NM.SendUDPInput(mask, currentTime);

    // 2. PROCESAR RED
    NM.RetryUnackedPackets(currentTime);
    NM.ListenUDP();

    // 3. ACTUALIZAR TRANSFORMS CON DATOS DEL SERVIDOR
    if (NM.newStateReceived) {
        if (NM.lastState.timestamp >= lastServerTimestamp) {
            lastServerTimestamp = NM.lastState.timestamp;

            for (short i = 0; i < 2; i++) {
                PlayerState& s = NM.lastState.players[i];
                PlayerModel& m = playerModels[i];

                // Actualizamos el Transform base
                m.transform.position = Vector2((float)s.posX, (float)s.posY);

                // Flip arreglado (moonwalk) ajustando el scale de tu Transform
                if (s.velX < 0) m.transform.scale = Vector2(1.0f, 1.0f);
                else if (s.velX > 0) m.transform.scale = Vector2(-1.0f, 1.0f);

                // Seleccionamos qué Renderer usar
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
        }
        NM.newStateReceived = false;
    }

    // 4. ACTUALIZAR RENDERERS (Para que tu AnimatedImageRenderer cambie los frames con el tiempo)
    for (short i = 0; i < 2; i++) {
        if (playerModels[i].activeRenderer) {
            playerModels[i].activeRenderer->Update();
        }
    }
}

void GameScene::Render() {
    Scene::Render();

    // Llamamos al Render de tu motor
    for (short i = 0; i < 2; i++) {
        if (playerModels[i].activeRenderer) {
            playerModels[i].activeRenderer->Render();
        }
    }
}

void GameScene::OnExit() {
    Scene::OnExit();
}