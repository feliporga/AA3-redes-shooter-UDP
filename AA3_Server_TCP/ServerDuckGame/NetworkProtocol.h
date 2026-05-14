#pragma once
#include <cstdint>

// Tipo de paquete optimizado a short
enum class PacketType : short {
    LoginRequest,
    LoginSuccess,
    LoginFailed,
    RegisterRequest,
    RegisterSuccess,
    RegisterFailed,
    RankingRequest,
    RankingResponse,
    CreateRoomRequest,
    JoinRoomRequest,
    RoomSuccess,
    RoomError,
    GameStart,
    GameMove,
    UpdateBoard,
    ReportResult,
    ResultAcknowledge
};

// Bitmask para las teclas (2 bytes)
enum InputFlags : short {
    INPUT_NONE = 0,
    INPUT_LEFT = 1,
    INPUT_RIGHT = 2,
    INPUT_JUMP = 4,
    INPUT_SHOOT = 8,
    INPUT_TAUNT = 16
};

#pragma pack(push, 1) 
// Paquete de Input (Cliente -> Servidor UDP)
struct C2S_InputPacket {
    short type = (short)PacketType::GameMove;
    short playerID;
    unsigned int timestamp;
    short inputMask;
};

// Estado de cada jugador
struct PlayerState {
    short id;
    short posX;
    short posY;
    short velX;
    short velY;
    bool onGround;
    bool facingRight;
};

// Paquete de Estado (Servidor UDP -> Cliente)
struct S2C_StatePacket {
    short type = (short)PacketType::UpdateBoard;
    unsigned int timestamp;
    PlayerState players[2]; // 1v1
};
#pragma pack(pop)