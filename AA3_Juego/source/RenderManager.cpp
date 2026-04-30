#include "RenderManager.h"
#include <iostream>
#include <cassert>

void RenderManager::Init()
{
    try {
        CreateWindow();
        // SetBackground("resources/background1.png"); 
    }
    catch (std::exception& exception)
    {
        std::cout << exception.what();
        return;
    }
}

void RenderManager::Release()
{
    if (window) {
        window->close();
        delete window;
    }
}

void RenderManager::ClearScreen()
{
    // Limpia la pantalla en color negro
    window->clear(sf::Color::Black);
}

void RenderManager::RenderScreen()
{
    // Muestra lo que se ha dibujado en el frame
    window->display();
}

void RenderManager::LoadTexture(std::string path)
{
    if (textures.find(path) != textures.end())
        return;

    sf::Texture* newTexture = new sf::Texture();
    if (!newTexture->loadFromFile(path)) {
        std::cerr << "Error cargando textura: " << path << std::endl;
        delete newTexture;
        return;
    }

    textures[path] = newTexture;
}

sf::Texture* RenderManager::GetTexture(std::string path)
{
    if (textures.find(path) != textures.end())
        return textures[path];
    return nullptr;
}

void RenderManager::LoadFont(std::string path)
{
    if (fonts.find(path) != fonts.end())
        return;

    sf::Font* newFont = new sf::Font();
    // SFML 3.0: Cambiamos loadFromFile por openFromFile
    if (!newFont->openFromFile(path)) {
        std::cerr << "Error cargando fuente: " << path << std::endl;
        delete newFont;
        return;
    }

    fonts[path] = newFont;
}

sf::Font* RenderManager::GetFont(std::string path)
{
    if (fonts.find(path) != fonts.end())
        return fonts[path];
    return nullptr;
}

RenderManager::~RenderManager()
{
    for (auto const& [key, val] : textures) {
        delete val;
    }
    textures.clear();

    for (auto const& [key, val] : fonts) {
        delete val;
    }
    fonts.clear();

    if (backgroundTexture) delete backgroundTexture;
    if (backgroundSprite) delete backgroundSprite;
}

void RenderManager::CreateWindow()
{
    window = new sf::RenderWindow(sf::VideoMode({ WINDOW_WIDTH, WINDOW_HEIGHT }), "3 en Raya Online");
    window->setFramerateLimit(60);
}

void RenderManager::DrawRect(int x, int y, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    sf::RectangleShape rect(sf::Vector2f((float)w, (float)h));
    rect.setPosition({ (float)x, (float)y }); 
    rect.setFillColor(sf::Color(r, g, b, a));

    if (window) {
        window->draw(rect);
    }
}

void RenderManager::SetBackground(const std::string& path)
{
    if (backgroundTexture) {
        delete backgroundTexture;
        delete backgroundSprite;
    }

    backgroundTexture = new sf::Texture();
    if (backgroundTexture->loadFromFile(path)) {
        backgroundSprite = new sf::Sprite(*backgroundTexture);
    }
}

void RenderManager::RenderBackground()
{
    if (backgroundSprite) {
        window->draw(*backgroundSprite);
    }
}