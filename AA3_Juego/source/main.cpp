#include "Game.h"
#include "InputManager.h"

#include <exception>
#include <iostream>

#define Input InputManager::Instance()

int main() {
    Game game;

    srand((unsigned int)time(NULL));

    try
    {
        game.Init();
    }
    catch (std::exception& exception)
    {
        std::cout << "Error: " << exception.what() << std::endl;
        game.Release();
        return -1;
    }

    bool playing = true;
    while (playing)
    {
        TIME.Update();

        if (TIME.ShouldUpdateGame())
        {
            playing = !Input.Listen();

            game.Update();
            game.Render();

            TIME.ResetDeltaTime();
        }
    }

    game.Release();

    return 0;
}