#pragma once
#include <SFML/System.hpp>
#include <cmath>

#define TIME TimeManager::Instance()

class TimeManager {
private:
    sf::Clock clock;

    float deltaTime = 0.0f;
    float elapsedTime = 0.0f;
    float previousElapsedTime = 0.0f;

    static const int FPS = 60;
    float frameTime;

    TimeManager() {
        previousElapsedTime = clock.getElapsedTime().asSeconds();
        frameTime = 1.0f / (float)FPS;
    }
    ~TimeManager() = default;

    TimeManager(const TimeManager& t) = delete;
    TimeManager& operator=(const TimeManager& t) = delete;

public:
    inline float GetDeltaTime() const { return deltaTime; }
    inline float GetElapsedTime() const { return elapsedTime; }

    inline bool ShouldUpdateGame() const { return deltaTime >= frameTime; }
    inline void ResetDeltaTime() {
        deltaTime = deltaTime - float(floor(deltaTime / frameTime) * frameTime);
    }

    static TimeManager& Instance() {
        static TimeManager tm;
        return tm;
    }

    void Update() {
        // SFML nos da el tiempo directamente en segundos con mucha precision
        elapsedTime = clock.getElapsedTime().asSeconds();
        deltaTime += elapsedTime - previousElapsedTime;
        previousElapsedTime = elapsedTime;
    }
};