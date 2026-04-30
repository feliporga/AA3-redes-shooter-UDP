#pragma once

#pragma once

enum class PacketType {
    //LOGIN / REGISTER
    LoginRequest,
    LoginSuccess,
    LoginFailed,
    RegisterRequest,
    RegisterSuccess,
    RegisterFailed,
    //RANKING
    RankingRequest,
    RankingResponse,
    //SALAS
    CreateRoomRequest,
    JoinRoomRequest,
    RoomSuccess,
    RoomError,
    GameStart,
    //GAMEPLAY
    GameMove,
    UpdateBoard,

    //ISTEMA DE RESULTADOS Y ELO
    ReportResult,
    ResultAcknowledge
};