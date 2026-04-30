#include "NetworkManager.h"
#include "Game.h"
#include "RenderManager.h"
#include "SceneManager.h"
#include "AudioManager.h"
#include "LoginScene.h"
#include "MainMenu.h"
#include "TicTacToe.h"
#include "YouWonScene.h"
#include "RankingScene.h"
#include "GameOverScene.h"

void Game::Init() {
    NM.ConnectToServer(SERVER_IP, SERVER_PORT);
    RM->Init();

    AM.LoadSong("menuMusic");

    SM.AddScene("Login", new LoginScene());
    SM.AddScene("MainMenu", new MainMenu());
    SM.AddScene("GameOver", new GameOverScene());
    SM.AddScene("YouWon", new YouWonScene());
    SM.AddScene("TicTacToe", new TicTacToe());
    SM.AddScene("Ranking", new RankingScene());

    SM.InitFirstScene("Login");
}

void Game::Update()
{
    NM.Listen();
    SM.UpdateCurrentScene();
}

void Game::Render()
{
    RM->ClearScreen();

    RM->RenderBackground();

    SM.GetCurrentScene()->Render();

    RM->RenderScreen();
}

void Game::Release()
{
    SM.GetCurrentScene()->OnExit();
    RM->Release();
}
