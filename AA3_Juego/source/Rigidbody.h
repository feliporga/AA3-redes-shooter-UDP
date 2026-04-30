#pragma once
#include <vector>
#include "AABB.h"
#include "Transform.h"
#include "TimeManager.h"

class Rigidbody {
private:
	std::vector <AABB*> colliders;
	Transform* transform;

	Vector2 velocity;
	float angularVelocity;

	Vector2 acceleration;
	float angularAcceleration;

	float linearDrag;
	float angularDrag;

public:
	Rigidbody(Transform* transform)
		: transform(transform), velocity(Vector2()), angularVelocity(0.0f), acceleration(Vector2()),
		angularAcceleration(0.0f), linearDrag(0.0f), angularDrag(0.0f) {}
	~Rigidbody()
	{
		for (AABB* collider : colliders)
			delete collider;
	}

	inline void AddCollider(AABB* collider) { colliders.push_back(collider); }
	
	bool CheckCollision(const Rigidbody* other) {
		for (AABB* myCol : colliders) {
			for (AABB* otherCol : other->colliders) {
				if (myCol->CheckOverlappingAABB(otherCol))
					return true;
			}
		}
		return false;
	}

	bool CheckOverlappingPoint(Vector2 point) {
		for (AABB* myCol : colliders) {
			if (myCol->CheckOverlappingPoint(point))
				return true;
		}
		return false;
	}

	void Update() {
		// 1. Update velocity
		velocity = velocity + acceleration * TIME.GetDeltaTime();
		angularVelocity = angularVelocity + angularAcceleration * TIME.GetDeltaTime();

		// 2. Update DRAG
		velocity = velocity * (1.0f / (1.0f + TIME.GetDeltaTime() * linearDrag));
		angularVelocity = angularVelocity * (1.0f / (1.0f + TIME.GetDeltaTime() * angularDrag));


		// 3. Update position
		transform->position = transform->position + velocity * TIME.GetDeltaTime();
		transform->rotation = transform->rotation + angularVelocity * TIME.GetDeltaTime();
		
		//4. Reset aceleration
		acceleration = Vector2();
		angularAcceleration = 0.0f;

		// 5. Check collision
		for (AABB* col : colliders) {
			col->SetTopLeft(transform->position);
			col->SetSize(transform->size * transform->scale);
		}
	}

	inline void AddForce(Vector2 force) { acceleration = acceleration + force;  }

	inline void AddTorque(float amount) { angularAcceleration = angularAcceleration + amount; }

	inline Vector2 GetVelocity() { return velocity; }

	inline void SetVelocity(Vector2 velocity) { this->velocity = velocity; }

	inline void SetLinearDrag(float linearDrag) { this->linearDrag = linearDrag; }

	inline void SetAngularDrag(float angularDrag) { this->angularDrag = angularDrag; }
};