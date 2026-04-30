#include "TicTacToe.h"
#include "InputManager.h"
#include "RenderManager.h"
#include "NetworkManager.h"
#include "Spawner.h"

TicTacToe::TicTacToe() : Scene() {
    startX = STARTX;
    startY = STARTY;
    cellSize = CELLSIZE;

    statusText = nullptr;
    backButton = nullptr;
    backButtonText = nullptr;
}

std::string TicTacToe::GetPlayerName(int player) {
    if (player == 1) return "JUGADOR 1 (CUADRADO)";
    if (player == 2) return "JUGADOR 2 (TRIANGULO)";
    if (player == 3) return "JUGADOR 3 (CIRCULO)";
    if (player == 4) return "JUGADOR 4 (CRUZ)";
    return " ";
}

bool TicTacToe::CheckWin(int p) {
    // Horizontales
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 4; c++) {
            if (board[r][c] == p && board[r][c + 1] == p && board[r][c + 2] == p) return true;
        }
    }
    // Verticales
    for (int c = 0; c < 6; c++) {
        for (int r = 0; r < 4; r++) {
            if (board[r][c] == p && board[r + 1][c] == p && board[r + 2][c] == p) return true;
        }
    }
    // Diagonales principales (\)
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (board[r][c] == p && board[r + 1][c + 1] == p && board[r + 2][c + 2] == p) return true;
        }
    }
    // Diagonales inversas (/)
    for (int r = 0; r < 4; r++) {
        for (int c = 2; c < 6; c++) {
            if (board[r][c] == p && board[r + 1][c - 1] == p && board[r + 2][c - 2] == p) return true;
        }
    }
    return false;
}

void TicTacToe::HandleInput() {
    if (gameOver) return;

    // no clic si ya has ganado //ayuda ia
    if (std::find(podium.begin(), podium.end(), myPlayerID) != podium.end()) {
        return;
    }

    bool isLeftClick = Input.GetLeftClick();

    if (isLeftClick && !mouseHeld) {
        mouseHeld = true;

        float mouseX = (float)Input.GetMouseX();
        float mouseY = (float)Input.GetMouseY();

        if (mouseX >= 50 && mouseX <= 260 && mouseY >= 50 && mouseY <= 100) {
            std::cout << "[GAME] Abanadonando la partida..." << std::endl;
            NM.LeaveMatchAndReconnect();
            SM.SetNextScene("MainMenu");
            return;
        }

        if (mouseX >= startX && mouseX <= startX + 600 && mouseY >= startY && mouseY <= startY + 600) {
            int col = (int)((mouseX - startX) / cellSize);
            int row = (int)((mouseY - startY) / cellSize);

            if (currentPlayer == myPlayerID && board[row][col] == 0) {

                
                // Saltarse quien esta en el podio // ayuda ia comentarios para debug
                int nextTurn = myPlayerID;
                do {
                    nextTurn = (nextTurn % 4) + 1;
                } while (std::find(podium.begin(), podium.end(), nextTurn) != podium.end() && podium.size() < 3);

                std::cout << "[ESCENA] Proximo Turno calculado: " << nextTurn << std::endl;

                ApplyMoveFromServer(row, col, myPlayerID, nextTurn);

                // Pasamos el turno
                NM.SendGameMove(row, col, myPlayerID, nextTurn);
            }
            else if (currentPlayer != myPlayerID) {
                std::cout << "[CLIENTE] Tranquilo, que todavia es el turno del Jugador " << currentPlayer << "!" << std::endl;
            }
        }
    }
    else if (!isLeftClick) {
        mouseHeld = false;
    }
}

bool TicTacToe::IsMyTurn(int nextPlayerTurn) {
    return myPlayerID == nextPlayerTurn;
}

void TicTacToe::ApplyMoveFromServer(int row, int col, int playerWhoMoved, int nextPlayerTurn) {

    // Resetear el cron�metro 
    turnTimer = INITIAL_TIMER;

    //Movimiento time out
    if (row == -1 && col == -1) {
        std::cout << "[ESCENA] Omitiendo turno de " << playerWhoMoved << " por inactividad." << std::endl;
    }
    else {

        // LINEA DE DEBUG POR IA PARA DETECTAR ERROR
        std::cout << "[ESCENA] Aplicando ficha del Jugador " << playerWhoMoved << ". Yo soy el ID " << myPlayerID << " y le toca al ID " << nextPlayerTurn << std::endl;

        board[row][col] = playerWhoMoved;
        movesCount++;

        Vector2 pos(startX + col * cellSize, startY + row * cellSize);
        PlayerPiece* newSprite = new PlayerPiece(playerWhoMoved, pos);

        SPAWN.SpawnObject(newSprite);
        cellSprites[row][col] = newSprite;


        if (CheckWin(playerWhoMoved)) {
            //  a�adimos al podio
            if (std::find(podium.begin(), podium.end(), playerWhoMoved) == podium.end()) {
                podium.push_back(playerWhoMoved);
                std::cout << "[GAME] El jugador " << playerWhoMoved << " entra al podio en la posicion " << podium.size() << std::endl;
            }
        }
    }

    CheckAndSendResults();

   
    if (!gameOver) {
        if (movesCount >= BOARD_SIZE * BOARD_SIZE) {
            // En un tablero de 6x6 (36 casillas) si se llena antes de que ganen 3 personas
            statusText->SetText("EMPATE - TABLERO LLENO");
            gameOver = true;
        }
        else {
            currentPlayer = nextPlayerTurn; 
            std::cout << "[ESCENA] Jugador actual es: " << currentPlayer << std::endl;

            // estoy podio?
            bool iAmInPodium = (std::find(podium.begin(), podium.end(), myPlayerID) != podium.end());

            if (iAmInPodium) {
                statusText->SetText("�HAS GANADO! ESPERANDO AL RESTO...");
                statusText->SetColor(sf::Color::Cyan);
            }
            else if (currentPlayer == myPlayerID) {
                statusText->SetText("TU TURNO");
                statusText->SetColor(sf::Color::Green);
            }
            else {
                statusText->SetText("TURNO: " + GetPlayerName(currentPlayer));
                statusText->SetColor(sf::Color::White);
            }
        }
    }
}

void TicTacToe::OnEnter() {
    isMyTurn = NM.nextGameMyTurn;
    opponentName = NM.nextGameOpponent;
    myPlayerID = NM.nextGamePlayerID;

    currentPlayer = 1;
    gameOver = false;

    podium.clear();         
    hasSentResult = false;

    returnToLobbyTimer = 5.0f;

    mouseHeld = Input.GetLeftClick();

    movesCount = 0;

    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 6; c++) {
            board[r][c] = 0;
            cellSprites[r][c] = nullptr;
        }
    }

    //timer logica
    turnTimer = INITIAL_TIMER;
    timerText = new TextObject("TIEMPO: " + std::to_string((int)INITIAL_TIMER));
  
    timerText->GetTransform()->position = Vector2(RM->WINDOW_WIDTH / 2 - 60, 720.0f);
    timerText->SetColor(sf::Color::Red);
    SPAWN.SpawnObject(timerText);


    statusText = new TextObject("TURNO: " + GetPlayerName(currentPlayer));
    statusText->GetTransform()->position = Vector2(RM->WINDOW_WIDTH / 2 - 200, 30);

    if (myPlayerID == currentPlayer) {
        statusText->SetText("TU TURNO");
        statusText->SetColor(sf::Color::Green);
    }
    else {
        statusText->SetText("TURNO: " + GetPlayerName(currentPlayer));
        statusText->SetColor(sf::Color::White);
    }
    SPAWN.SpawnObject(statusText);

    backButton = new Button(BUTTON_POSITION, BUTTON_SIZE, COLOR_BACK_BUTTON, "", Button::ActionType::ChangeScene, "MainMenu");
    SPAWN.SpawnObject(backButton);

    backButtonText = new TextObject("VOLVER");
    backButtonText->GetTransform()->position = BACK_TEXT_POSITION;
    SPAWN.SpawnObject(backButtonText);

    std::string roomCode = SM.sharedData;
    if (roomCode == "") {
        roomCode = "SALA PUBLICA";
    }
    else {
        roomCode = "SALA: " + roomCode;
    }

    roomCodeText = new TextObject(roomCode);
    roomCodeText->GetTransform()->position = ROOM_CODE_POSITION;
    roomCodeText->SetColor(sf::Color::Cyan);
    SPAWN.SpawnObject(roomCodeText);
}

void TicTacToe::OnExit() {
    Scene::OnExit();
}

void TicTacToe::Update() {
    Scene::Update();

    //ir al lobby
    if (gameOver) {
        returnToLobbyTimer -= TIME.GetDeltaTime();

        if (returnToLobbyTimer <= 0.0f) {
            NM.ResetP2P(); // Destruimos los sockets 
            SM.SetNextScene("MainMenu"); 
        }
    }
    // cronometro
    else if (podium.size() < 3) {
        turnTimer -= TIME.GetDeltaTime();

        if (timerText) {
            int secondsLeft = std::max(0, (int)std::ceil(turnTimer));
            timerText->SetText("TIEMPO: " + std::to_string(secondsLeft));
        }

        if (turnTimer <= 0.0f) {
            turnTimer = 20.0f;
            std::cout << "[GAME] Tiempo agotado para el Jugador " << currentPlayer << ". Saltando turno..." << std::endl;

            int nextTurn = currentPlayer;
            do {
                nextTurn = (nextTurn % 4) + 1;
            } while (std::find(podium.begin(), podium.end(), nextTurn) != podium.end() && podium.size() < 3);

            ApplyMoveFromServer(-1, -1, currentPlayer, nextTurn);
        }
    }

    HandleInput();
}

void TicTacToe::Render() {
    Scene::Render();

    for (int i = 0; i <= 6; i++) {
        RM->DrawRect((int)(startX + i * cellSize), (int)startY, 4, 600, 255, 255, 255, 255);
        RM->DrawRect((int)startX, (int)(startY + i * cellSize), 600, 4, 255, 255, 255, 255);
    }
}


void TicTacToe::CheckAndSendResults() {
    if (gameOver || hasSentResult) return;

    // acabar al haber 3 players en el podio
    if (podium.size() == 3) {
        gameOver = true;
        hasSentResult = true;

        // No podio = pierde
        int loserID = 0;
        for (int i = 1; i <= 4; i++) {
            if (std::find(podium.begin(), podium.end(), i) == podium.end()) {
                loserID = i;
                break;
            }
        }
        podium.push_back(loserID);

        std::cout << "[GAME] Partida terminada. Enviando resultados al servidor..." << std::endl;
        NM.SendMatchResult(SM.sharedData, podium[0], podium[1], podium[2], podium[3]);

        //Orden de victoria
        std::string victoriaStr = "1ro: J" + std::to_string(podium[0]) +
            " | 2do: J" + std::to_string(podium[1]) +
            " | 3ro: J" + std::to_string(podium[2]) +
            " | 4to: J" + std::to_string(podium[3]);

        statusText->SetText(victoriaStr);
        statusText->SetColor(sf::Color::Yellow);
        statusText->GetTransform()->position = Vector2(RM->WINDOW_WIDTH / 2 - 320, 30); 

        if (timerText) timerText->SetText(""); 
    }
}