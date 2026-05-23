#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"

class MainMenu : public Scene {
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
    MainMenu();
    virtual ~MainMenu() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};