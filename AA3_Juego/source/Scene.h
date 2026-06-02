#pragma once
#include "Object.h"
#include "Spawner.h"
#include <vector>

class Scene {
protected:
    std::vector<Object*> ui;
    std::vector<Object*> objects;

public:
    Scene() = default;
    virtual void OnEnter() = 0;
    virtual void OnExit() {
        SPAWN.ClearSpawnedObject();

        for (Object* o : objects)
            delete o;
        objects.clear();
    }
    virtual void Update() {
        //DESTRUCTION
        for (short i = objects.size() - 1; i >= 0; i--) {
            if (objects[i]->IsPendingDestroy()) {
                delete objects[i];
                objects.erase(objects.begin() + i); //esto es para borrar el objetos despues de borrar su puntero
            }
        }

        //SPAWNING
        while (SPAWN.GetSpawnedObjectCount() > 0) {
            objects.push_back(SPAWN.GetSpawnedObject());
        }

        //UPDATING
        for (Object* o : objects)
            o->Update();

        for (short i = 0; i < objects.size(); i++)
        {
            for (short j = i + 1; j < objects.size(); j++)
            {
                if (objects[i]->GetRigidbody()->CheckCollision(objects[j]->GetRigidbody()))
                {
                    objects[i]->OnCollisionEnter(objects[j]);
                    objects[j]->OnCollisionEnter(objects[i]);
                }
            }
        }
    }

    virtual void Render() {
        for (Object* o : objects)
            o->Render();
    }
};