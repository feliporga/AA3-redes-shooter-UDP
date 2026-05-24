#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"
#include <vector>
#include <string>

#define TEXT_COLOR sf::Color(200, 200, 200, 255)
#define BUTTON_POSITION Vector2(50.0f, 50.0f)
#define BUTTON_SIZE Vector2(260.0f, 50.0f)
#define BUTTON_COLOR sf::Color(0, 100, 200, 255)

// Estructura para manejar cada columna individualmente y garantizar la alineación
struct RankRowUI {
    TextObject* pos;
    TextObject* name;
    TextObject* elo;
    TextObject* wins;
    TextObject* losses;
    TextObject* indicator; // Texto para el "TU POSICION ->"
};

class RankingScene : public Scene {
private:
    TextObject* titleText;

    // Cabeceras separadas como pedías
    TextObject* headerPos;
    TextObject* headerName;
    TextObject* headerElo;
    TextObject* headerWins;
    TextObject* headerLosses;

    Button* backButton;
    TextObject* backButtonText;

    // Vector que guarda las filas formadas por columnas individuales
    std::vector<RankRowUI> tableRows;

public:
    RankingScene();
    ~RankingScene() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};