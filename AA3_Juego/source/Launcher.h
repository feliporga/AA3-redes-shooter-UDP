#pragma once
#include <SFML/Network.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include "NetworkProtocol.h"
#include "NetworkManager.h"

class Launcher {
public:
    Launcher() = default;
    ~Launcher() = default;

    bool Run();
};