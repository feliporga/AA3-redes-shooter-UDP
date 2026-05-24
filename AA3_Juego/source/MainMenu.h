#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"

class MainMenu : public Scene {
private:
    float winW;
    float winH;

    // title
    TextObject* titleText;
    TextObject* statusText;

    // game
    Button* btnCasual;
    Button* btnCompetitive;
    Button* btnCancel;
    TextObject* txtCasual;
    TextObject* txtCompetitive;
    TextObject* txtCancel;

    // ranking
    Button* btnRanking;
    TextObject* txtRanking;

    // audio
    Button* btnMuteMusic;
    TextObject* txtMuteMusic;
    Button* btnMuteSFX;
    TextObject* txtMuteSFX;

    bool isSearching;

public:
    static bool isMusicMuted;
    static bool isSfxMuted;
    static bool isMenuMusicPlaying;

    MainMenu();
    virtual ~MainMenu() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};