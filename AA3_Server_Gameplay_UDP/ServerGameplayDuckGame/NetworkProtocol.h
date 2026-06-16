#pragma once
#include <cstdint>

#define NORMAL_PACKET   0b00000000 
#define CRITICAL_PACKET 0b00000001
#define URGENT_PACKET   0b00000010

#define MAX_BULLETS 20

// Numero maximo de irregularidades antes de dar la partida por perdida
#define MAX_IRREGULARITIES 3

//stats player 

const short INITIAL_LIVES = 3;
const short INITIAL_HP = 5;
const short BULLET_DAMAGE = 1;
const short RESPAWN_POS_X = 400;
const short RESPAWN_POS_Y = 100;
const short MAP_FLOOR_Y = 500;
const short PLAYER_HITBOX_W = 32;
const short PLAYER_HITBOX_H = 32;
const short BULLET_HITBOX_W = 10;
const short BULLET_HITBOX_H = 10;
const short PLAYER_JUMP_SPEED = -15;
const short BULLET_SPEED = 15;
const short BULLET_SPAWN_OFFSET = 20;
const short DEATH_Y_LIMIT = 1000;

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
    RoomSuccess,
    RoomError,
    MatchCreatedNotification,
    GameStart,
    GameMove,
    UpdateBoard,
    ReportResult,
    ResultAcknowledge,
    Ack,
    Ping,
    Pong,
    SearchCasualRequest,
    SearchCompetitiveRequest,
    CancelSearchRequest,
    CheckMapVersion,
    UpdateMap,
    MapUpToDate,
    TimeoutWarning
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
    unsigned short packetID = 0;
    short type = (short)PacketType::GameMove;
    short playerID;
    short inputMask;
    unsigned short matchID = 0;

    // Identifica cada input normal para poder hacer prediccion y reconciliacion.
    unsigned short movementID = 0;

    // Posicion predicha por el cliente para que el servidor pueda validarla como en el tema de movimiento
    bool hasPredictedPosition = false;
    short predictedPosX = 0;
    short predictedPosY = 0;
};

// Paquete de Confirmacion (Servidor -> Cliente UDP)
struct S2C_AckPacket {
    uint8_t flags = NORMAL_PACKET;
    short type = (short)PacketType::Ack;
    unsigned short ackedPacketID;
};

// Paquete Ping (Servidor UDP -> Cliente)
// El servidor lo envia de forma periodica para comprobar que el cliente sigue vivo.
// timestamp guarda el tiempo de envio del servidor y luego sirve para calcular el RTT.
struct S2C_PingPacket {
    uint8_t flags = NORMAL_PACKET;
    short type = (short)PacketType::Ping;
    unsigned short matchID = 0;
    short playerID = 0;
    unsigned short pingID = 0;
    unsigned int timestamp = 0;
};

// Paquete Pong (Cliente -> Servidor UDP)
// El cliente responde al Ping manteniendo el mismo pingID y el mismo timestamp del Ping.
// Asi el servidor puede calcular RTT = tiempoActualServidor - timestampPing.
struct C2S_PongPacket {
    uint8_t flags = NORMAL_PACKET;
    short type = (short)PacketType::Pong;
    unsigned short matchID = 0;
    short playerID = 0;
    unsigned short pingID = 0;
    unsigned int timestamp = 0;
};

struct PlayerState {
    short id;
    short posX;
    short posY;
    short velX;
    short velY;
    bool onGround;
    bool facingRight;

    bool isTaunting;

    short hp;
    short lives;

    //ultimo move player
    unsigned short lastProcessedMovementID = 0;
};

struct BulletState {
    bool active;
    short posX;
    short posY;
    short velX;
    short ownerID;
};

struct S2C_TimeoutWarningPacket {
    uint8_t flags = URGENT_PACKET; // Lo marcamos como urgente
    short type = (short)PacketType::TimeoutWarning;
};

struct S2C_StatePacket {
    uint8_t flags = NORMAL_PACKET;
    short type = (short)PacketType::UpdateBoard;
    unsigned int timestamp;
    PlayerState players[2];
    BulletState bullets[MAX_BULLETS];
    bool isGameOver = false;
    short winnerID = 0;
    unsigned short matchID = 0;

    // Estos datos permiten avisar al cliente cuando el servidor detecta una irregularidad
    bool correctionRequired = false;
    short irregularPlayerID = 0;
    short irregularityCount = 0;
    unsigned short irregularityEventID = 0;

    // Indica que el game over se ha producido al alcanzar tres irregularidades
    bool gameOverByIrregularities = false;

    // Datos de reconciliacion tipo BACK/ID como en el tema de movimiento
    bool movementBackRequired = false;
    short backPlayerID = 0;
    unsigned short backMovementID = 0;
    short backPosX = 0;
    short backPosY = 0;
};
#pragma pack(pop)