#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"
#include <SFML/Graphics.hpp>

struct PlayerModel {
    sf::Sprite* sprite = nullptr; // Estilo ImageRenderer
    sf::Texture* texIdle = nullptr;
    sf::Texture* texWalk = nullptr;
    sf::Texture* texJump = nullptr;

    PlayerModel() : sprite(nullptr), texIdle(nullptr), texWalk(nullptr), texJump(nullptr) {}
};

class GameScene : public Scene {
private:
    PlayerModel playerModels[2];
    sf::Clock gameClock;
    unsigned int lastServerTimestamp;

    TextObject* statusText;
    Button* backButton;

    void LoadPlayerAssets(short index, short playerID);

public:
    GameScene();
    virtual ~GameScene(); // Destructor para limpiar punteros

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};