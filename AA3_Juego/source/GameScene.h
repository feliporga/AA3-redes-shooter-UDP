#pragma once
#include "Scene.h"
#include "TextObject.h"
#include "Button.h"
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>

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

    // Posicion predicha que el cliente ha enviado al servidor para validacion
    short posX;
    short posY;

    short velX;
	short velY;
    bool onGround;
};

// Estado recibido del rival para poder mostrarlo interpolado
struct RemoteSnapshot {
    unsigned int timestamp;
    Vector2 position;

    // Se guarda el ID para tener relacion con el movimiento validado por el servidor
    unsigned short movementID;
};

class GameScene : public Scene {
private:
    PlayerModel playerModels[2];
    sf::Clock gameClock;
    // Este reloj controla la vuelta al menu despues del resultado
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

    // Este mensaje muestra durante unos segundos la irregularidad detectada por el servidor
    sf::Clock irregularityMessageClock;
    bool showIrregularityMessage;
    unsigned short lastIrregularityEventID;
    std::string irregularityMessage;

    // Datos de fisica local para que la prediccion envie posicion, no solo input
    short localPredictedVelX;
    short localPredictedVelY;
    bool localPredictedOnGround;

    // Mapa local usado para que la prediccion del cliente choque parecido al servidor
    std::vector<std::vector<short>> localLevelMap;
    short localMapRows;
    short localMapCols;

    TextObject* statusText;
    void LoadPlayerAssets(short index, short playerID);
    void LoadMap();

    // Predicción, reconciliación e interpolación del movimiento online
    // Simula un tick de movimiento local; se reutiliza para prediccion y para re-simular inputs pendientes.
    void SimulateLocalMovement(short inputMask, Transform& transform, short& velX, short& velY, bool& onGround);
    void ApplyLocalPrediction(short inputMask);
    void ReconcileLocalPlayer(const PlayerState& serverPlayer);
    void AddRemoteSnapshot(const PlayerState& serverPlayer, unsigned int timestamp);
    void InterpolateRemotePlayer(unsigned int currentTime);

    // Funciones auxiliares de prediccion local contra el mapa
    bool CheckLocalTileCollision(short x, short y, short w, short h);
    bool CheckLocalGround(short x, short y, short w, short h);
    void InitializeLocalPredictionFromServer(const PlayerState& serverPlayer);

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