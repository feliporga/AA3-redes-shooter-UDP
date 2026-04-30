#include "ImageRenderer.h"
#include "RenderManager.h"

ImageRenderer::ImageRenderer(Transform* transform, std::string resourcePath, Vector2 sourceOffset, Vector2 sourceSize)
    : Renderer(transform, resourcePath)
{
    RM->LoadTexture(resourcePath);
    sf::Texture* tex = RM->GetTexture(resourcePath);


    if (tex) {
        sprite = new sf::Sprite(*tex);
    }

    sourceRect = sf::IntRect({ (int)sourceOffset.x, (int)sourceOffset.y }, { (int)sourceSize.x, (int)sourceSize.y });

    if (sprite) {
        sprite->setTextureRect(sourceRect);
    }
}

ImageRenderer::~ImageRenderer()
{
    if (sprite) {
        delete sprite;
        sprite = nullptr;
    }
}

void ImageRenderer::Update()
{
    if (!sprite) return; // Si no hay sprite, no actualizamos

    Vector2 offset = (Vector2(-transform->size.x, -transform->size.y) / 2.0f) * transform->scale;

    sprite->setPosition({ transform->position.x + offset.x, transform->position.y + offset.y });
    sprite->setScale({ transform->scale.x, transform->scale.y });
    sprite->setRotation(sf::degrees(transform->rotation));
}

void ImageRenderer::Render()
{
    if (sprite && RM->GetWindow()) {
        RM->GetWindow()->draw(*sprite); // Le pasamos el contenido del puntero
    }
}