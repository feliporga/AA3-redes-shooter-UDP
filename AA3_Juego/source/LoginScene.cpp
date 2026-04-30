#include "LoginScene.h"
#include "SceneManager.h"
#include "RenderManager.h"
#include "Spawner.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "InputManager.h"

LoginScene::LoginScene() : Scene() {
    winW = RM->WINDOW_WIDTH;
    winH = RM->WINDOW_HEIGHT;

    titleText = nullptr;
    userField = nullptr;
    passField = nullptr;
    btnLogin = nullptr;
    btnRegister = nullptr;
    userLabel = nullptr;
    passLabel = nullptr;
    btnLoginText = nullptr;
    btnRegisterText = nullptr;
}

void LoginScene::OnEnter() {

	//INICIALIZACIÓN DE OBJETOS UI
    titleText = new TextObject("3 EN RAYA ONLINE");
    titleText->GetTransform()->position = Vector2(winW / 2 - 190.0f, winH / 2 - 200);
    titleText->SetColor(sf::Color::Yellow);
    SPAWN.SpawnObject(titleText);

    userField = new TextField(Vector2(winW / 2 - 200, winH / 2 - 100), Vector2(400, 45));
    passField = new TextField(Vector2(winW / 2 - 200, winH / 2 - 20), Vector2(400, 45));
    SPAWN.SpawnObject(userField);
    SPAWN.SpawnObject(passField);

    userLabel = new TextObject("NICKNAME:");
    userLabel->GetTransform()->position = Vector2(winW / 2 - 115, winH / 2 - 130);
    SPAWN.SpawnObject(userLabel);

    passLabel = new TextObject("PASSWORD:");
    passLabel->GetTransform()->position = Vector2(winW / 2 - 115, winH / 2 - 50);
    SPAWN.SpawnObject(passLabel);

    btnLogin = new Button(Vector2(winW / 2 - 125, winH / 2 + 60), Vector2(BTN_WIDTH, BTN_HEIGHT), COLOR_LOGIN, "", Button::ActionType::None);
    btnRegister = new Button(Vector2(winW / 2 - 125, winH / 2 + 120), Vector2(BTN_WIDTH, BTN_HEIGHT), COLOR_REGISTER, "", Button::ActionType::None);

    SPAWN.SpawnObject(btnLogin);
    SPAWN.SpawnObject(btnRegister);

    btnLoginText = new TextObject("LOGIN");
    btnLoginText->GetTransform()->position = Vector2(winW / 2 - 60.0f, winH / 2 + 70);
    SPAWN.SpawnObject(btnLoginText);

    btnRegisterText = new TextObject("REGISTRO");
    btnRegisterText->GetTransform()->position = Vector2(winW / 2 - 100.0f, winH / 2 + 130);
    SPAWN.SpawnObject(btnRegisterText);
}

void LoginScene::OnExit() {
    Scene::OnExit();
}

void LoginScene::Update() {
    Scene::Update();

    // Mouse click edge detection
    static bool prevLeftClick = false;
    bool currentLeftClick = Input.GetLeftClick();

    if (currentLeftClick && !prevLeftClick) {
        float mouseX = (float)Input.GetMouseX();
        float mouseY = (float)Input.GetMouseY();

        float btnX = winW / 2 - 125;
        float btnW = BTN_WIDTH;
        float btnH = BTN_HEIGHT;

        // Login button hitbox and network request
        float loginY = winH / 2 + 60;
        if (mouseX >= btnX && mouseX <= btnX + btnW &&
            mouseY >= loginY && mouseY <= loginY + btnH)
        {
            std::string user = userField->GetText();
            std::string pass = passField->GetText();

            if (user != "" && pass != "") {
                NM.SendLogin(user, pass);
                std::cout << "[CLIENTE] Login enviado";
            }
            else {
                std::cout << "[CLIENTE] Por favor, rellena usuario y contrasena." << std::endl;
            }
        }

        // Register button hitbox and network request
        float regY = winH / 2 + 120;
        if (mouseX >= btnX && mouseX <= btnX + btnW &&
            mouseY >= regY && mouseY <= regY + btnH)
        {
            std::string user = userField->GetText();
            std::string pass = passField->GetText();

            if (user != "" && pass != "") {
                NM.SendRegister(user, pass);
            }
            else {
                std::cout << "[CLIENTE] Por favor, rellena usuario y contrasena para registrarte." << std::endl;
            }
        }
    }

    prevLeftClick = currentLeftClick;
}

void LoginScene::Render() {
    Scene::Render();
}