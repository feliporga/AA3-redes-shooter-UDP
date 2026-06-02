#pragma once
#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>
#include "RenderManager.h"
#include <unordered_map>
#include <string>

#define Input InputManager::Instance()

enum KeyState { EMPTY, DOWN, UP, HOLD, RELEASED };

class InputManager {
private:
    short mouseX, mouseY;
    bool leftClick;
    std::string textBuffer;
    std::unordered_map<sf::Keyboard::Key, KeyState> keyReference;

    InputManager() : mouseX(0), mouseY(0), leftClick(false) {}

public:
    static InputManager& Instance() {
        static InputManager instance;
        return instance;
    }

    bool Listen() {
        textBuffer.clear();
        for (auto& kv : keyReference) {
            if (kv.second == DOWN) kv.second = HOLD;
            else if (kv.second == UP) kv.second = RELEASED;
        }

        if (RM->GetWindow()) {
            sf::Vector2i mousePos = sf::Mouse::getPosition(*RM->GetWindow());
            mouseX = mousePos.x;
            mouseY = mousePos.y;

            while (const std::optional event = RM->GetWindow()->pollEvent()) {
                if (event->is<sf::Event::Closed>()) return true;

                if (const auto* textEntered = event->getIf<sf::Event::TextEntered>()) {
                    if (textEntered->unicode < 128) {
                        textBuffer += static_cast<char>(textEntered->unicode);
                    }
                }

                else if (const auto* mouseBtn = event->getIf<sf::Event::MouseButtonPressed>()) {
                    if (mouseBtn->button == sf::Mouse::Button::Left) leftClick = true;
                }
                else if (const auto* mouseBtnRel = event->getIf<sf::Event::MouseButtonReleased>()) {
                    if (mouseBtnRel->button == sf::Mouse::Button::Left) leftClick = false;
                }
                else if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                    if (keyReference[keyPressed->code] != HOLD) keyReference[keyPressed->code] = DOWN;
                }
                else if (const auto* keyReleased = event->getIf<sf::Event::KeyReleased>()) {
                    keyReference[keyReleased->code] = UP;
                }
            }
        }
        return false;
    }

    inline short GetMouseX() const { return mouseX; }
    inline short GetMouseY() const { return mouseY; }
    inline bool GetLeftClick() const { return leftClick; }
    inline std::string GetTextBuffer() const { return textBuffer; }
    inline bool GetEvent(sf::Keyboard::Key input, KeyState inputValue) { return keyReference[input] == inputValue; }
};