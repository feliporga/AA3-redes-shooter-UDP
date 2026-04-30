#pragma once
#include "Object.h"
#include "TextRenderer.h"

class TextObject : public Object
{
private:
    TextRenderer* tr;

public:
    TextObject(std::string text) : Object() {
        tr = new TextRenderer(transform, text);
        renderer = tr;
        tr->SetColor(sf::Color::White); // Cambio a color de SFML
    }

    ~TextObject() {
        tr = nullptr;
    }

    void SetText(std::string text) {
        tr->SetText(text);
    }

    void SetColor(sf::Color color) {
        tr->SetColor(color);
    }
};