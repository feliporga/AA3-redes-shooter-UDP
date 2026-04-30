#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include "Transform.h"

class Renderer {
protected:
    sf::Color color = sf::Color::White;
    Transform* transform;

    // SFML 3.0 usa IntRect para recortar texturas (píxeles enteros)
    sf::IntRect sourceRect;
    std::string targetPath;

public:
    Renderer(Transform* transform, std::string targetPath) {
        this->transform = transform;
        this->targetPath = targetPath;
    }
    virtual ~Renderer() {}

    virtual void Update() = 0;
    virtual void Render() = 0;
    virtual void SetColor(sf::Color color) {
        this->color = color;
    };
    sf::Color GetColor() { return color; }
};