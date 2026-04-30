#pragma once
#include "Transform.h"
#include "Rigidbody.h"
#include "ImageRenderer.h"
#include "AnimatedImageRenderer.h"
#include "TimeManager.h"

class Object
{
private:
	bool isPendingDestroy = false;

protected:
	Renderer* renderer = nullptr;
	Transform* transform;
	Rigidbody* physics;

public:
	Object()
	{
		transform = new Transform();
		physics = new Rigidbody(transform);
	};

	~Object() {
		delete transform;
		delete physics;
		delete renderer;
	}

	virtual void Update() {
		if (physics != nullptr)
			physics->Update();

		renderer->Update();
	}
	virtual void Render()
	{
		renderer->Render();
	}

	Transform* GetTransform() { return transform; }
	Rigidbody* GetRigidbody() { return physics; }

	bool IsPendingDestroy() { return isPendingDestroy; }
	virtual void Destroy() {
		isPendingDestroy = true;
	}
	virtual void OnCollisionEnter(Object* other) {}
};