#include "MainMenu.h"
#include "SceneManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include "NetworkManager.h"
#include "InputManager.h"
#include <iostream>

bool MainMenu::isMusicMuted = false;
bool MainMenu::isSfxMuted = false;
bool MainMenu::isMenuMusicPlaying = false; // Inicializamos en falso

MainMenu::MainMenu() : Scene() {
    winW = (float)RM->WINDOW_WIDTH;
    winH = (float)RM->WINDOW_HEIGHT;
    titleText = nullptr; statusText = nullptr;
    btnCasual = nullptr; btnCompetitive = nullptr; btnCancel = nullptr;
    txtCasual = nullptr; txtCompetitive = nullptr; txtCancel = nullptr;

    btnRanking = nullptr; txtRanking = nullptr;
    btnMuteMusic = nullptr; txtMuteMusic = nullptr;
    btnMuteSFX = nullptr; txtMuteSFX = nullptr;

    isSearching = false;
}

void MainMenu::OnEnter() {
    // --- MAGIA: Solo reproducimos si no está muteada Y si no estaba sonando ya ---
    if (!isMusicMuted && !isMenuMusicPlaying) {
        AM.PlaySong("menuMusic");
        isMenuMusicPlaying = true; // Avisamos que ya está sonando
        std::cout << "[CLIENTE] Playing Menu Music" << std::endl;
    }

    isSearching = false;

    titleText = new TextObject("MATCHMAKING MULTIPLAYER");
    titleText->GetTransform()->position = Vector2(winW / 2.0f - 290.0f, winH / 2.0f - 160.0f);
    titleText->SetColor(sf::Color::Yellow);
    SPAWN.SpawnObject(titleText);

    statusText = new TextObject("SELECT GAME MODE TO START SEARCH");
    statusText->GetTransform()->position = Vector2(winW / 2.0f - 360.0f, winH / 2.0f - 100.0f);
    statusText->SetColor(sf::Color::White);
    SPAWN.SpawnObject(statusText);

    float btnWidth = 600.0f;
    float btnX = winW / 2.0f - (btnWidth / 2.0f);

    btnCasual = new Button(Vector2(btnX, winH / 2.0f - 30.0f), Vector2(btnWidth, 50.0f), sf::Color::Blue, "", Button::ActionType::None);
    btnCompetitive = new Button(Vector2(btnX, winH / 2.0f + 40.0f), Vector2(btnWidth, 50.0f), sf::Color::Magenta, "", Button::ActionType::None);
    btnCancel = new Button(Vector2(btnX, winH / 2.0f + 110.0f), Vector2(btnWidth, 50.0f), sf::Color::Red, "", Button::ActionType::None);
    btnRanking = new Button(Vector2(btnX, winH / 2.0f + 180.0f), Vector2(btnWidth, 50.0f), sf::Color(0, 150, 0), "", Button::ActionType::None);

    SPAWN.SpawnObject(btnCasual);
    SPAWN.SpawnObject(btnCompetitive);
    SPAWN.SpawnObject(btnCancel);
    SPAWN.SpawnObject(btnRanking);

    txtCasual = new TextObject("CASUAL MATCH (NO RANK)");
    txtCasual->GetTransform()->position = Vector2(winW / 2.0f - 250.0f, winH / 2.0f - 15.0f);
    SPAWN.SpawnObject(txtCasual);

    txtCompetitive = new TextObject("COMPETITIVE MATCH (ELO)");
    txtCompetitive->GetTransform()->position = Vector2(winW / 2.0f - 260.0f, winH / 2.0f + 55.0f);
    SPAWN.SpawnObject(txtCompetitive);

    txtCancel = new TextObject("CANCEL SEARCH");
    txtCancel->GetTransform()->position = Vector2(winW / 2.0f - 180.0f, winH / 2.0f + 125.0f);
    SPAWN.SpawnObject(txtCancel);

    txtRanking = new TextObject("VIEW RANKING");
    txtRanking->GetTransform()->position = Vector2(winW / 2.0f - 160.0f, winH / 2.0f + 195.0f);
    SPAWN.SpawnObject(txtRanking);

    btnMuteMusic = new Button(Vector2(20.0f, 20.0f), Vector2(220.0f, 40.0f), sf::Color(100, 100, 100), "", Button::ActionType::None);
    txtMuteMusic = new TextObject(isMusicMuted ? "MUSIC: OFF" : "MUSIC: ON");
    txtMuteMusic->GetTransform()->position = Vector2(35.0f, 25.0f);
    SPAWN.SpawnObject(btnMuteMusic);
    SPAWN.SpawnObject(txtMuteMusic);

    btnMuteSFX = new Button(Vector2(20.0f, 70.0f), Vector2(220.0f, 40.0f), sf::Color(100, 100, 100), "", Button::ActionType::None);
    txtMuteSFX = new TextObject(isSfxMuted ? "SFX: OFF" : "SFX: ON");
    txtMuteSFX->GetTransform()->position = Vector2(35.0f, 75.0f);
    SPAWN.SpawnObject(btnMuteSFX);
    SPAWN.SpawnObject(txtMuteSFX);
}

void MainMenu::Update() {
    Scene::Update();

    // Comprobamos si ha empezado la partida
    if (NM.gameStarted) {
        NM.gameStarted = false;

        // --- PARAR AUDIO AQUÍ ---
        // Al entrar al juego apagamos la música del menú y reseteamos el estado
        AM.StopAudio();
        isMenuMusicPlaying = false;

        SM.SetNextScene("GameScene");
        return;
    }

    static bool prevLeftClick = false;
    bool currentLeftClick = Input.GetLeftClick();

    if (currentLeftClick && !prevLeftClick) {
        float mx = (float)Input.GetMouseX();
        float my = (float)Input.GetMouseY();

        float bx = winW / 2.0f - 300.0f;

        if (mx >= bx && mx <= bx + 600.0f && my >= (winH / 2.0f + 180.0f) && my <= (winH / 2.0f + 230.0f)) {
            SM.SetNextScene("Ranking");
            return;
        }

        // HITBOX DE MUTEAR MUSICA
        if (mx >= 20.0f && mx <= 240.0f && my >= 20.0f && my <= 60.0f) {
            isMusicMuted = !isMusicMuted;
            if (isMusicMuted) {
                txtMuteMusic->SetText("MUSIC: OFF");
                AM.StopAudio();
                isMenuMusicPlaying = false; // Actualizamos estado
            }
            else {
                txtMuteMusic->SetText("MUSIC: ON");
                AM.PlaySong("menuMusic");
                isMenuMusicPlaying = true;  // Actualizamos estado
            }
        }

        // HITBOX DE MUTEAR SFX
        if (mx >= 20.0f && mx <= 240.0f && my >= 70.0f && my <= 110.0f) {
            isSfxMuted = !isSfxMuted;
            if (isSfxMuted) {
                txtMuteSFX->SetText("SFX: OFF");
            }
            else {
                txtMuteSFX->SetText("SFX: ON");
            }
        }

        if (!isSearching) {
            if (mx >= bx && mx <= bx + 600.0f && my >= (winH / 2.0f - 30.0f) && my <= (winH / 2.0f + 20.0f)) {
                NM.SendSearchCasual();
                isSearching = true;
                statusText->SetText("SEARCHING CASUAL MATCHMAKING...");
                statusText->SetColor(sf::Color::Cyan);
            }
            else if (mx >= bx && mx <= bx + 600.0f && my >= (winH / 2.0f + 40.0f) && my <= (winH / 2.0f + 90.0f)) {
                NM.SendSearchCompetitive(1000);
                isSearching = true;
                statusText->SetText("SEARCHING RANKED OPPONENT (ELO)...");
                statusText->SetColor(sf::Color::Magenta);
            }
        }
        else {
            if (mx >= bx && mx <= bx + 600.0f && my >= (winH / 2.0f + 110.0f) && my <= (winH / 2.0f + 160.0f)) {
                NM.CancelSearch();
                isSearching = false;
                statusText->SetText("SELECT GAME MODE TO START SEARCH");
                statusText->SetColor(sf::Color::White);
            }
        }
    }
    prevLeftClick = currentLeftClick;
}

void MainMenu::OnExit() {
    // Hemos quitado el AM.StopAudio(); para que la música siga fluyendo al cambiar al Ranking
    Scene::OnExit();
}

void MainMenu::Render() {
    Scene::Render();
}