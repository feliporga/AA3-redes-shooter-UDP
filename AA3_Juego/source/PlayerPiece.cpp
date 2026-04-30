#include "PlayerPiece.h"
#include "RenderManager.h"

std::string PlayerPiece::GetSpritePath(int player) {
    if (player == 1) return "resources/player1.png";
    if (player == 2) return "resources/player2.png";
    if (player == 3) return "resources/player3.png";
    if (player == 4) return "resources/player4.png";
    return "";
}

PlayerPiece::PlayerPiece(int player, const Vector2& position)
    : Object(), playerID(player)
{
    transform->position = position + Vector2(50.0f, 50.0f);

    std::string path = GetSpritePath(player);
    RM->LoadTexture(path);

    renderer = new ImageRenderer(transform, path, Vector2(0.0f, 0.0f), Vector2(PIECE_SIZE, PIECE_SIZE));

    transform->size = Vector2(PIECE_SIZE, PIECE_SIZE);

    transform->scale = Vector2(100.0f / PIECE_SIZE, 100.0f / PIECE_SIZE);
}