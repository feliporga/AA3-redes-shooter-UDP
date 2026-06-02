#pragma once
#include "queue"
#include "Object.h"

#define SPAWN Spawner::Instance()

class Spawner
{
private:
	std::queue<Object*> spawnedObjects;

	Spawner() = default;
	Spawner(Spawner&) = default;

	Spawner& operator = (const Spawner&) = delete;

public:
	inline static Spawner& Instance()
	{
		static Spawner spawner;
		return spawner;
	}

	inline void SpawnObject(Object* obj)
	{
		spawnedObjects.push(obj);
	}

	inline Object* GetSpawnedObject()
	{
		if (spawnedObjects.empty())
			return nullptr;
		
		Object* o = spawnedObjects.front();
		spawnedObjects.pop();
		return o;
	}

	inline short GetSpawnedObjectCount()
	{
		return spawnedObjects.size();
	}

	inline void ClearSpawnedObject()
	{
		while (!spawnedObjects.empty())
		{
			delete spawnedObjects.front();
			spawnedObjects.pop();
		}
	}
};