#pragma once
#include "Vector2.h"

class Transform {
public:
    Vector2 position;
    float rotation;
    Vector2 scale;

    Vector2 size;

public:
    Transform() : position(Vector2(0, 0)), rotation(0), scale(Vector2(1, 1)), size(Vector2(100, 100)) {}
    Transform(Vector2 _position, float _rotation, Vector2 _scale, Vector2 _size) : 
        position(_position), rotation(_rotation), scale(_scale){}
};