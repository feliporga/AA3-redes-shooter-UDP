#include "RankingScene.h"
#include "SceneManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include "NetworkManager.h"
#include "InputManager.h"

RankingScene::RankingScene() : Scene() {
    titleText = nullptr;
    headerPos = nullptr; headerName = nullptr; headerElo = nullptr;
    headerWins = nullptr; headerLosses = nullptr;
    backButton = nullptr; backButtonText = nullptr;
}

void RankingScene::OnEnter() {
    NM.RequestRanking();

    float winW = (float)RM->WINDOW_WIDTH;

    // --- COORDENADAS X ABSOLUTAS PARA ALINEACIÓN PERFECTA ---
    float COL_POS_X = winW / 2.0f - 250.0f;
    float COL_NAME_X = winW / 2.0f - 150.0f;
    float COL_ELO_X = winW / 2.0f + 120.0f;
    float COL_WINS_X = winW / 2.0f + 220.0f;
    float COL_LOSSES_X = winW / 2.0f + 300.0f;

    // Botón Volver
    backButton = new Button(BUTTON_POSITION, BUTTON_SIZE, BUTTON_COLOR, "", Button::ActionType::None);
    SPAWN.SpawnObject(backButton);

    backButtonText = new TextObject("VOLVER");
    backButtonText->GetTransform()->position = Vector2(BUTTON_POSITION.x + 50.0f, BUTTON_POSITION.y + 10.0f);
    SPAWN.SpawnObject(backButtonText);

    // Título
    titleText = new TextObject("TOP 10 MEJORES JUGADORES");
    titleText->GetTransform()->position = Vector2(winW / 2.0f - 220.0f, 40.0f);
    titleText->SetColor(sf::Color::Yellow);
    SPAWN.SpawnObject(titleText);

    // Cabeceras Separadas
    float headerY = 100.0f;
    sf::Color headerColor = sf::Color::Cyan;

    headerPos = new TextObject("POS");
    headerPos->GetTransform()->position = Vector2(COL_POS_X, headerY);
    headerPos->SetColor(headerColor);
    SPAWN.SpawnObject(headerPos);

    headerName = new TextObject("NOMBRE");
    headerName->GetTransform()->position = Vector2(COL_NAME_X, headerY);
    headerName->SetColor(headerColor);
    SPAWN.SpawnObject(headerName);

    headerElo = new TextObject("ELO");
    headerElo->GetTransform()->position = Vector2(COL_ELO_X, headerY);
    headerElo->SetColor(headerColor);
    SPAWN.SpawnObject(headerElo);

    headerWins = new TextObject("V");
    headerWins->GetTransform()->position = Vector2(COL_WINS_X, headerY);
    headerWins->SetColor(headerColor);
    SPAWN.SpawnObject(headerWins);

    headerLosses = new TextObject("D");
    headerLosses->GetTransform()->position = Vector2(COL_LOSSES_X, headerY);
    headerLosses->SetColor(headerColor);
    SPAWN.SpawnObject(headerLosses);

    // Pre-creamos las 11 filas (5 TextObjects por cada fila)
    float startY = 150.0f;
    float spacing = 40.0f;
    tableRows.clear();

    for (short i = 0; i < 11; i++) {
        RankRowUI row;
        float currentY = startY + (i * spacing);

        // Fila 11 separada del resto (Posicion del cliente)
        if (i == 10) currentY += 30.0f;

        row.pos = new TextObject("");
        row.pos->GetTransform()->position = Vector2(COL_POS_X, currentY);
        SPAWN.SpawnObject(row.pos);

        row.name = new TextObject("");
        row.name->GetTransform()->position = Vector2(COL_NAME_X, currentY);
        SPAWN.SpawnObject(row.name);

        row.elo = new TextObject("");
        row.elo->GetTransform()->position = Vector2(COL_ELO_X, currentY);
        SPAWN.SpawnObject(row.elo);

        row.wins = new TextObject("");
        row.wins->GetTransform()->position = Vector2(COL_WINS_X, currentY);
        SPAWN.SpawnObject(row.wins);

        row.losses = new TextObject("");
        row.losses->GetTransform()->position = Vector2(COL_LOSSES_X, currentY);
        SPAWN.SpawnObject(row.losses);

        // --- CORRECCIÓN AQUÍ ---
        // Desplazamos el indicador a -350.0f para que nunca pise los números de la tabla
        row.indicator = new TextObject("");
        row.indicator->GetTransform()->position = Vector2(COL_POS_X - 350.0f, currentY);
        SPAWN.SpawnObject(row.indicator);

        tableRows.push_back(row);
    }
}

void RankingScene::Update() {
    Scene::Update();

    if (NM.newRankingAvailable) {
        NM.newRankingAvailable = false;

        // Limpiamos todos los textos por si acaso
        for (RankRowUI& row : tableRows) {
            row.pos->SetText("");
            row.name->SetText("");
            row.elo->SetText("");
            row.wins->SetText("");
            row.losses->SetText("");
            row.indicator->SetText("");
        }

        std::string myName = NM.GetSavedUser();

        // Rellenamos las columnas por separado
        for (size_t i = 0; i < NM.lastRanking.size() && i < 11; i++) {
            PlayerRanking& rank = NM.lastRanking[i];
            RankRowUI& row = tableRows[i];

            row.pos->SetText(std::to_string(rank.pos));
            row.name->SetText(rank.name);
            row.elo->SetText(std::to_string(rank.pts));
            row.wins->SetText(std::to_string(rank.v));
            row.losses->SetText(std::to_string(rank.d));

            // Lógica de colores y posición 11
            sf::Color rowColor = TEXT_COLOR;

            if (i == 10) {
                rowColor = sf::Color::Green;
                row.indicator->SetText("TU POSICION ->");
                row.indicator->SetColor(rowColor);
            }
            else if (rank.name == myName) {
                rowColor = sf::Color::Yellow;
            }

            // Aplicamos el color a toda la fila
            row.pos->SetColor(rowColor);
            row.name->SetColor(rowColor);
            row.elo->SetColor(rowColor);
            row.wins->SetColor(rowColor);
            row.losses->SetColor(rowColor);
        }
    }

    // Hitbox Botón Volver
    static bool prevLeftClick = false;
    bool currentLeftClick = Input.GetLeftClick();

    if (currentLeftClick && !prevLeftClick) {
        float mx = (float)Input.GetMouseX();
        float my = (float)Input.GetMouseY();

        float bx = BUTTON_POSITION.x;
        float by = BUTTON_POSITION.y;
        float bw = BUTTON_SIZE.x;
        float bh = BUTTON_SIZE.y;

        if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
            SM.SetNextScene("MainMenu");
            return;
        }
    }
    prevLeftClick = currentLeftClick;
}

void RankingScene::Render() { Scene::Render(); }
void RankingScene::OnExit() { Scene::OnExit(); }