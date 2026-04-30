#pragma once
#include "Scene.h"
#include "TextField.h"
#include "Button.h"
#include "TextObject.h"
#define BTN_WIDTH 250.0f
#define BTN_HEIGHT 45.0f
#define COLOR_LOGIN sf::Color(0, 100, 200, 255)
#define COLOR_REGISTER sf::Color(0, 150, 0, 255)

class LoginScene : public Scene {
private:
    float winW;
    float winH;

    TextObject* titleText;

    TextField* userField;
    TextField* passField;

    Button* btnLogin;
    Button* btnRegister;

    TextObject* userLabel;
    TextObject* passLabel;
    TextObject* btnLoginText;
    TextObject* btnRegisterText;

public:
    LoginScene();
    ~LoginScene() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};