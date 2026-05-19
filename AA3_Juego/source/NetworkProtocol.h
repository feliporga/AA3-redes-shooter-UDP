#pragma once
#include <cstdint>

#define NORMAL_PACKET   0b00000000 
#define CRITICAL_PACKET 0b00000001
#define URGENT_PACKET   0b00000010


#define MAX_BULLETS 20



//stats plpayer 

constexpr short INITIAL_LIVES = 3;
constexpr short INITIAL_HP = 5;
constexpr short BULLET_DAMAGE = 1;
constexpr short RESPAWN_POS_X = 400;
constexpr short RESPAWN_POS_Y = 100;
constexpr short MAP_FLOOR_Y = 500;
constexpr short PLAYER_HITBOX_W = 32;
constexpr short PLAYER_HITBOX_H = 32;
constexpr short BULLET_HITBOX_W = 10;
constexpr short BULLET_HITBOX_H = 10;

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
    ResultAcknowledge,
    Ack
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
    uint8_t flags = NORMAL_PACKET;
    unsigned int packetID = 0;
    short type = (short)PacketType::GameMove;
    short playerID;
    unsigned int timestamp;
    short inputMask;
};

// Paquete de Confirmación (Servidor -> Cliente UDP)
struct S2C_AckPacket {
    uint8_t flags = NORMAL_PACKET;
    short type = (short)PacketType::Ack;
    unsigned int ackedPacketID;
};

struct PlayerState {
    short id;
    short posX;
    short posY;
    short velX;
    short velY;
    bool onGround;
    bool facingRight;

    short hp;
    short lives;
};


struct BulletState {
    bool active;
    short posX;
    short posY;
    short velX;
    short ownerID;
};

struct S2C_StatePacket {
    uint8_t flags = NORMAL_PACKET;
    short type = (short)PacketType::UpdateBoard;
    unsigned int timestamp;
    PlayerState players[2];
    BulletState bullets[MAX_BULLETS];
    bool isGameOver = false;
    short winnerID = 0;
};
#pragma pack(pop)