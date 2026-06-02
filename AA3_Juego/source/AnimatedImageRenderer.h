#pragma once
#include "ImageRenderer.h"
#include "TimeManager.h"

class AnimatedImageRenderer : public ImageRenderer
{
private:
    short frameWidth;
    short frameHeight;
    short fps;
    bool looping;
    float frameTime;
    float currentFrameTime;
    short totalFrameCount;
    short currentFrameIndex;

public:
    AnimatedImageRenderer(short frameWidth, short frameHeight, short fps, bool looping,
        Transform* transform, std::string resourcePath, Vector2 sourceOffset,
        Vector2 sourceSize)
        : ImageRenderer(transform, resourcePath, sourceOffset, sourceSize),
        frameWidth(frameWidth), frameHeight(frameHeight), fps(fps), looping(looping)
    {
        frameTime = 1.0f / fps;
        currentFrameTime = 0.0f;
        totalFrameCount = (short)(sourceSize.x / frameWidth);
        currentFrameIndex = 0;

        sourceRect = sf::IntRect({ (short)sourceOffset.x, (short)sourceOffset.y }, { frameWidth, frameHeight });

        // CORRECCIÓN AQUÍ: Usamos -> porque sprite ahora es un puntero
        if (sprite) {
            sprite->setTextureRect(sourceRect);
        }
    }

    ~AnimatedImageRenderer() = default;

    virtual void Update() override {
        currentFrameTime += TIME.GetDeltaTime();

        if (currentFrameTime >= frameTime) {
            currentFrameTime -= frameTime;
            currentFrameIndex++;

            if (currentFrameIndex >= totalFrameCount) {
                currentFrameIndex = looping ? 0 : totalFrameCount - 1;
            }

            sourceRect.position.x = currentFrameIndex * frameWidth;
            sourceRect.size.x = frameWidth;
            sourceRect.size.y = frameHeight;

            // CORRECCIÓN AQUÍ TAMBIÉN
            if (sprite) {
                sprite->setTextureRect(sourceRect);
            }
        }

        ImageRenderer::Update();
    }

    virtual void Render() override { ImageRenderer::Render(); }
};