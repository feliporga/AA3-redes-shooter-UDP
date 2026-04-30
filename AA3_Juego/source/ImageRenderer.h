#pragma once
#include "Renderer.h"
#include <SFML/Graphics.hpp>

class ImageRenderer : public Renderer {
protected:
    sf::Sprite* sprite = nullptr; // Lo cambiamos a puntero para SFML 3.0

public:
    ImageRenderer(Transform* transform, std::string resourcePath, Vector2 sourceOffset, Vector2 sourceSize);

    // Necesitamos un destructor para borrar el puntero
    virtual ~ImageRenderer();

    virtual void Update() override;
    virtual void Render() override;
};