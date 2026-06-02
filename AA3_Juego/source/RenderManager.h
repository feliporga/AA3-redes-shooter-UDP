#pragma once
#include <map>
#include <string>
#include <SFML/Graphics.hpp>

#define RM RenderManager::GetInstance()

class RenderManager {
public:
    static RenderManager* GetInstance() {
        static RenderManager instance;
        return &instance;
    }

private:
    sf::RenderWindow* window;

    std::map<std::string, sf::Texture*> textures;
    std::map<std::string, sf::Font*> fonts;

    sf::Texture* backgroundTexture = nullptr;
    sf::Sprite* backgroundSprite = nullptr;

public:
    void Init();
    void Release();
    void ClearScreen();
    void RenderScreen();

    sf::RenderWindow* GetWindow() { return window; }

    const unsigned short WINDOW_WIDTH = 1360;
    const unsigned short WINDOW_HEIGHT = 820;

    void LoadTexture(std::string path);
    sf::Texture* GetTexture(std::string path);

    void LoadFont(std::string path);
    sf::Font* GetFont(std::string path);

    void DrawRect(short x, short y, short w, short h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

    void SetBackground(const std::string& path);
    void RenderBackground();

private:
    RenderManager() = default;
    RenderManager(RenderManager&) = delete;
    RenderManager& operator= (const RenderManager&) = delete;
    ~RenderManager();

    void CreateWindow();
};