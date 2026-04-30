#pragma once
#include "Object.h"
#include "RenderManager.h"
#include "InputManager.h"
#include "SceneManager.h"
#include "AudioManager.h"
#include <string>
#include <SFML/Graphics.hpp>

class Button : public Object {
public:
    enum class ActionType {
        ChangeScene,
        ToggleAudio,
        SpritesChange,
        ExitGame,
        None
    };

private:
    Vector2 size;
    sf::Color color;
    std::string text;
    ActionType actionType;
    std::string targetScene;
    TextRenderer* textRenderer = nullptr;

    bool prevLeftClick;

    int windowWidth = RM->WINDOW_WIDTH;
    int windowHeight = RM->WINDOW_HEIGHT;

public:
    Button(const Vector2& position,
        const Vector2& size,
        const sf::Color& color,
        const std::string& text,
        ActionType actionType,
        const std::string& targetScene = "")
        : size(size), color(color), text(text), actionType(actionType), targetScene(targetScene)
    {
        transform->position = position;
        transform->scale = Vector2(1.0f, 1.0f);
        textRenderer = new TextRenderer(transform, text);
        textRenderer->SetColor(sf::Color::White);

        prevLeftClick = Input.GetLeftClick();
    }

    ~Button() {
        delete textRenderer;
        textRenderer = nullptr;
    }

    void Update() override {
        float mouseX = (float)Input.GetMouseX();
        float mouseY = (float)Input.GetMouseY();
        bool currentLeftClick = Input.GetLeftClick();

        bool isMouseOver =
            mouseX >= transform->position.x && mouseX <= transform->position.x + size.x &&
            mouseY >= transform->position.y && mouseY <= transform->position.y + size.y;

        if (isMouseOver && currentLeftClick && !prevLeftClick) {
            switch (actionType) {
            case ActionType::ChangeScene:
                SM.SetNextScene(targetScene);
                break;
            case ActionType::ToggleAudio:
            {
                // Handle music state
                if (AM.GetMuted()) {
                    AM.UnMute();
                    AM.PlaySong("menuMusic");
                }
                else {
                    AM.Mute();
                    AM.StopAudio();
                }
                break;
            }
            case ActionType::ExitGame:
                SceneManager::Instance().ExitGame();
                break;
            case ActionType::None:
                // Do nothing, handled manually in the scene Update
                break;
            }
        }

        prevLeftClick = currentLeftClick;
    }

    void Render() override {
        RM->DrawRect((int)transform->position.x,
            (int)transform->position.y,
            (int)size.x,
            (int)size.y,
            color.r, color.g, color.b, color.a);

        if (textRenderer) {
            textRenderer->Render();
        }
    }
};