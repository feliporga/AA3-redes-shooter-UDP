#pragma once
#include "Renderer.h"
#include "RenderManager.h"
#include <SFML/Graphics.hpp>
#include <cassert>

#define FONT_PATH "resources/Fonts/PokemonClassic.ttf"

class TextRenderer : public Renderer
{
protected:
    std::string textString;
    sf::Text* sfText = nullptr;

public:
    TextRenderer(Transform* transform, std::string _text) : Renderer(transform, FONT_PATH)
    {
        color = sf::Color::White;
        SetText(_text);
    }

    ~TextRenderer() {
        if (sfText) {
            delete sfText;
            sfText = nullptr;
        }
    }

    virtual void SetColor(sf::Color newColor) override {
        color = newColor;
        if (sfText) {
            sfText->setFillColor(color);
        }
    }

    virtual void Update() override {
        if (sfText) {
            sfText->setPosition({ transform->position.x, transform->position.y });
            sfText->setScale({ transform->scale.x, transform->scale.y });
            sfText->setRotation(sf::degrees(transform->rotation));
        }
    }

    void Render() override
    {
        if (sfText && RM->GetWindow()) {
            RM->GetWindow()->draw(*sfText);
        }
    }

    void SetText(std::string newText) {
        RM->LoadFont(FONT_PATH);

        sf::Font* font = RM->GetFont(FONT_PATH);
        if (font) {
            if (!sfText) {
                sfText = new sf::Text(*font, newText, 24);
            }
            else {
                sfText->setFont(*font);
                sfText->setString(newText);
            }
            sfText->setFillColor(color);
        }

        textString = newText;
    }
};