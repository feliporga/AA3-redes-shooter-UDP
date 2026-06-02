#pragma once
#include "Object.h"

class ImageObject : public Object {
public:
	ImageObject(std::string texturePath, Vector2 sourceOffset, Vector2 sourceSize, short frameWidth, short frameHeight, short fps, bool looping)
		: Object() {
		physics->AddCollider(new AABB(sourceOffset, sourceSize));
		renderer = new AnimatedImageRenderer(frameWidth, frameHeight, fps, looping, transform, texturePath, sourceOffset, sourceSize);
	}
};