#pragma once
#include "Scene.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "RenderManager.h"
#include "Renderer.h"
#include "TextObject.h"
#include "Button.h"
#include "Vector2.h"

#include <iostream>
#include <string>

class YouWonScene : public Scene
{
private:
    float windowWidth = RM->WINDOW_WIDTH;
    float windowHeight = RM->WINDOW_HEIGHT;

    TextObject* youWonText = nullptr;

    Button* buttonMenu = nullptr;
    TextObject* buttonMenuText = nullptr;

public:
    void OnEnter() override
    {
        youWonText = new TextObject("YOU WON");
        youWonText->GetTransform()->position = Vector2(windowWidth / 2 - 80, windowHeight / 2 + 250);
        youWonText->GetTransform()->scale = Vector2(8.0f, 8.0f);

        SPAWN.SpawnObject(youWonText);


        buttonMenu = new Button(
            Vector2(windowWidth / 2 - 125.0f, windowHeight / 2 - 270),
            Vector2(250, 50),
            sf::Color(255, 0, 255, 255),
            "GO TO MENU",
            Button::ActionType::ChangeScene,
            "MainMenu"
        );
        SPAWN.SpawnObject(buttonMenu);

        buttonMenuText = new TextObject("GO TO MENU");
        buttonMenuText->GetTransform()->position = Vector2(windowWidth / 2 - 30.0f, windowHeight / 2 - 210);
        SPAWN.SpawnObject(buttonMenuText);
    }

    ~YouWonScene()
    {
        if (youWonText)
        {
            youWonText->Destroy();
            youWonText = nullptr;
        }
        if (buttonMenuText)
        {
            buttonMenuText->Destroy();
            buttonMenuText = nullptr;
        }
    }

    void OnExit() override
    {
        Scene::OnExit();
    }

    void Update() override
    {
        Scene::Update();
    }

    void Render() override
    {
        Scene::Render();
    }
};