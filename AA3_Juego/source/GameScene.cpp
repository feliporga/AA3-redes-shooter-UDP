#include "GameScene.h"
#include "NetworkManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include <iostream>
#include <fstream>
#include <sstream>

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
        std::stringstream ss(line);
        int tileType;
        int col = 0;

        while (ss >> tileType) {
            if (tileType == 1) {
                TileObject* tile = new TileObject(col * 32.0f, row * 32.0f);
                SPAWN.SpawnObject(tile);
            }
            col++;
        }
        row++;
    }
    file.close();
}

void GameScene::LoadPlayerAssets(short index, short playerID) {
    std::string character = (playerID == 1) ? "Mario" : "Luigi";

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

void GameScene::OnEnter() {
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
        bool currentOnGround = NM.lastState.players[myIndex].onGround;

        if (currentJump) {
            mask |= INPUT_JUMP;
            if ((!prevJump && currentOnGround) || (prevJump && !prevOnGround && currentOnGround)) {
                AM.PlayClip("marioJump", 0);
            }
        }

        prevJump = currentJump;
        prevOnGround = currentOnGround;

        NM.SendUDPInput(mask, currentTime);

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

            for (short i = 0; i < 2; i++) {
                PlayerState& s = NM.lastState.players[i];
                PlayerModel& m = playerModels[i];

                if (s.isTaunting && !prevTaunting[i]) {
                    std::cout << "[AUDIO] Jugador " << s.id << " hace la burla!" << std::endl;
                    AM.PlayClip("mariopapipo", 0);
                }
                prevTaunting[i] = s.isTaunting;

                m.transform.position = Vector2((float)s.posX, (float)s.posY);

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
            for (int i = 0; i < MAX_BULLETS; i++) {
                bulletActive[i] = NM.lastState.bullets[i].active;
                bulletTransforms[i].position = Vector2((float)NM.lastState.bullets[i].posX, (float)NM.lastState.bullets[i].posY);
            }
        }
        NM.newStateReceived = false;
    }

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