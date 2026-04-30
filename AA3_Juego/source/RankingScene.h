#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"
#include <vector>
#include <string>

#define TEXT_COLOR sf::Color(200, 200, 200, 255)
#define BUTTON_POSITION Vector2(50.0f, 50.0f)
#define BUTTON_SIZE Vector2(210.0f, 50.0f)
#define BUTTON_COLOR sf::Color(0, 100, 200, 255)
class RankingScene : public Scene {
private:
    TextObject* titleText;

    TextObject* headerName;
    TextObject* headerWins;
    TextObject* headerLosses;
    TextObject* headerElo;

    Button* backButton;
    TextObject* backButtonText;

    std::vector<TextObject*> tableTexts;

    void CreateDummyRow(float yOffset, std::string name, std::string wins, std::string losses, std::string elo);

public:
    RankingScene();
    ~RankingScene() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};