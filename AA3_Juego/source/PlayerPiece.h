#pragma once
#include "Object.h"
#include "ImageRenderer.h"
#include "Vector2.h"
#include <string>
#define PIECE_SIZE 293.0f
class PlayerPiece : public Object {
private:
    short playerID;

    static std::string GetSpritePath(short player);

public:
    PlayerPiece(short player, const Vector2& position);
    ~PlayerPiece() = default;

    short GetPlayerID() const { return playerID; }
};