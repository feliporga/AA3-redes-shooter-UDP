#pragma once
#include "Scene.h"

class Game 
{
public:
	Game() = default;

	void Init();
	void Update();
	void Render();
	void Release();

};