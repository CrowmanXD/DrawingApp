#include "ParameterInfluence.h"
#include <algorithm>

float ParameterInfluence::evaluate(const StrokePoint& point, float random) const {
    return strength;
}

float PressureInfluence::evaluate(const StrokePoint& point, float random) const {
    // Size/opacity scales linearly with pressure
    return strength * point.pressure;
}

float SpeedInfluence::evaluate(const StrokePoint& point, float random) const {
    // Clamp speed to min/max range
    float clampedSpeed = std::clamp(point.speed, minSpeed, maxSpeed);
    
    // Normalize to 0.0 - 1.0
    float normalized = (clampedSpeed - minSpeed) / (maxSpeed - minSpeed);
    
    // Inverse mapping: slower movement lays down more paint.
    return strength * (1.0f - normalized);
}

float RandomInfluence::evaluate(const StrokePoint& point, float random) const {
    // random parameter is 0.0 - 1.0
    return strength * random;
}
