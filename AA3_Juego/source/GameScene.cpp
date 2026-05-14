#include "GameScene.h"
#include "NetworkManager.h"
#include "RenderManager.h"
#include "Spawner.h"

GameScene::GameScene() : Scene() {
    statusText = nullptr;
    backButton = nullptr;
    lastServerTimestamp = 0;
}

GameScene::~GameScene() {
    // Limpieza de punteros estilo ImageRenderer
    for (short i = 0; i < 2; i++) {
        if (playerModels[i].sprite) {
            delete playerModels[i].sprite;
            playerModels[i].sprite = nullptr;
        }
    }
}

void GameScene::LoadPlayerAssets(short index, short playerID) {
    std::string character = (playerID == 1) ? "Mario" : "Luigi";

    std::string pathIdle = "resources/Idle" + character + ".png";
    std::string pathWalk = "resources/Walk" + character + ".png";
    std::string pathJump = "resources/Jump" + character + ".png";

    RM->LoadTexture(pathIdle);
    RM->LoadTexture(pathWalk);
    RM->LoadTexture(pathJump);

    playerModels[index].texIdle = RM->GetTexture(pathIdle);
    playerModels[index].texWalk = RM->GetTexture(pathWalk);
    playerModels[index].texJump = RM->GetTexture(pathJump);

    // Creamos el sprite con la textura inicial (Idle)
    if (playerModels[index].texIdle) {
        playerModels[index].sprite = new sf::Sprite(*(playerModels[index].texIdle));
        playerModels[index].sprite->setOrigin({ 16.f, 16.f });
    }
}

void GameScene::OnEnter() {
    LoadPlayerAssets(0, 1); // Jugador 1: Mario
    LoadPlayerAssets(1, 2); // Jugador 2: Luigi

    statusText = new TextObject("CONECTADO AL SERVER UDP");
    statusText->GetTransform()->position = Vector2(50, 50);
    SPAWN.SpawnObject(statusText);

    std::cout << "[UDP] Escena lista. Usando puerto " << SERVER_UDP_PORT << std::endl;
}

void GameScene::Update() {
    Scene::Update();

    // 1. Inputs (Bitmask)
    short mask = INPUT_NONE;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A))     mask |= INPUT_LEFT;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D))     mask |= INPUT_RIGHT;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space)) mask |= INPUT_JUMP;

    NM.SendUDPInput(mask, gameClock.getElapsedTime().asMilliseconds());

    // 2. Recibir Estado UDP
    S2C_StatePacket state;
    while (NM.ReceiveUDPState(state)) {
        if (state.timestamp < lastServerTimestamp) continue;
        lastServerTimestamp = state.timestamp;

        for (short i = 0; i < 2; i++) {
            PlayerState& s = state.players[i];
            PlayerModel& m = playerModels[i];
            if (!m.sprite) continue;

            // Posicion y escala (Estilo ImageRenderer: usando inicializadores {})
            m.sprite->setPosition({ (float)s.posX, (float)s.posY });

            // Cambio de textura segun estado del servidor
            if (!s.onGround) m.sprite->setTexture(*m.texJump);
            else if (s.velX != 0) m.sprite->setTexture(*m.texWalk);
            else m.sprite->setTexture(*m.texIdle);

            // Flip horizontal
            if (s.velX < 0) m.sprite->setScale({ 1.0f, 1.0f });
            else if (s.velX > 0) m.sprite->setScale({ -1.0f, 1.0f });
        }
    }
}

void GameScene::Render() {
    Scene::Render();
    // Dibujado manual usando el puntero a la ventana del RenderManager
    if (RM->GetWindow()) {
        for (short i = 0; i < 2; i++) {
            if (playerModels[i].sprite) {
                RM->GetWindow()->draw(*playerModels[i].sprite);
            }
        }
    }
}

void GameScene::OnExit() {
    Scene::OnExit();
}