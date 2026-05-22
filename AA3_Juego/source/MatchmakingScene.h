#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"

class MatchmakingScene : public Scene {
private:
    float winW;
    float winH;

    TextObject* titleText;
    TextObject* statusText;
    Button* btnCasual;
    Button* btnCompetitive;
    Button* btnCancel;
    TextObject* txtCasual;
    TextObject* txtCompetitive;
    TextObject* txtCancel;

    bool isSearching;

public:
    MatchmakingScene();
    virtual ~MatchmakingScene() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};