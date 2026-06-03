#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"
#include <SFML/Graphics.hpp>
#include <vector>

#include "NetworkProtocol.h"
#include "Transform.h"
#include "ImageRenderer.h"
#include "AnimatedImageRenderer.h"

struct PlayerModel {
    Transform transform;

    ImageRenderer* rendIdle = nullptr;
    AnimatedImageRenderer* rendWalk = nullptr;
    ImageRenderer* rendJump = nullptr;

    Renderer* activeRenderer = nullptr;
};

// Input local aplicado antes de recibir confirmación del servidor
struct PendingMovement {
    unsigned short movementID;
    short inputMask;
};

// Estado recibido del rival para poder mostrarlo interpolado
struct RemoteSnapshot {
    unsigned int timestamp;
    Vector2 position;
};

class GameScene : public Scene {
private:
    PlayerModel playerModels[2];
    sf::Clock gameClock;
    sf::Clock returnToMenuClock;
    bool returnToMenuTimerStarted;
    unsigned int lastServerTimestamp;

    // Evita aplicar predicción antes de recibir la primera posición oficial del servidor
    bool hasInitialServerState;

    // ID independiente de packetID: identifica movimientos normales pendientes de reconciliar
    unsigned short nextMovementID;
    std::vector<PendingMovement> pendingMovements;

    // Buffer de estados del rival, ordenado por timestamp para interpolar aunque UDP llegue desordenado
    std::vector<RemoteSnapshot> remoteSnapshots;
    bool hasServerTimeOffset;
    int serverTimeOffset;

    TextObject* statusText;
    void LoadPlayerAssets(short index, short playerID);
    void LoadMap();

    // Predicción, reconciliación e interpolación del movimiento online
    void ApplyLocalHorizontalPrediction(short inputMask);
    void ReconcileLocalPlayer(const PlayerState& serverPlayer);
    void AddRemoteSnapshot(const PlayerState& serverPlayer, unsigned int timestamp);
    void InterpolateRemotePlayer(unsigned int currentTime);

    //Bala
    Transform bulletTransforms[MAX_BULLETS];
    ImageRenderer* bulletRenderers[MAX_BULLETS];
    bool bulletActive[MAX_BULLETS];

public:
    GameScene();
    virtual ~GameScene();

    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    void Render() override;
};