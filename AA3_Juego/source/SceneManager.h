#pragma once
#include <unordered_map>
#include <string>
#include "Scene.h"

#define SM SceneManager::Instance()

class SceneManager
{
private:
	std::unordered_map<std::string, Scene*> scenes;
	Scene* currentScene = nullptr;
	std::string nextScene = "";

	SceneManager() = default;
	SceneManager(SceneManager&) = default;
	SceneManager& operator = (const SceneManager&) = delete;

public:
	std::string sharedData = "";
	inline static SceneManager& Instance()
	{
		static SceneManager sm;
		return sm;
	}

	inline bool AddScene(std::string name, Scene* scene)
	{
		if (scenes.find(name) == scenes.end())
		{
			scenes.emplace(name, scene);
			return true;
		}
		return false;
	}

	inline Scene* GetScene(std::string name)
	{
		if (scenes.find(name) != scenes.end())
			return scenes[name];

		return nullptr;
	}
	inline bool InitFirstScene(std::string name)
	{
		if (scenes.find(name) != scenes.end())
		{
			currentScene = scenes[name];
			currentScene->OnEnter();
			return true;
		}
		return false;
	}
	inline Scene* GetCurrentScene() { return currentScene; }
	inline bool SetNextScene(std::string name)
	{
		if (scenes.find(name) == scenes.end())
			return false;

		nextScene = name;
		return true;
	}
	inline void UpdateCurrentScene() 
	{
		if (nextScene != "")
		{
			currentScene->OnExit();
			currentScene = scenes[nextScene];
			currentScene->OnEnter();
			nextScene = "";
		}
		currentScene->Update();
	}

	inline void ExitGame() {
		exit(0);
	}
};
