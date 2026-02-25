#pragma once

#include "StrokePoint.h"

// Types of influences that can affect brush parameters
enum class InfluenceSource {
    Pressure,      // Pen pressure
    Speed,         // Cursor speed
    Tilt,          // Pen tilt angle
    Distance,      // Distance along stroke
    Random         // Random jitter
};

// Maps an influence source to a parameter value
struct ParameterInfluence {
    InfluenceSource source;
    float strength = 1.0f;     // How much this influences the parameter
    
    // Curve: simple linear, can be extended to support curves
    virtual float evaluate(const StrokePoint& point, float random = 0.5f) const;
    
    virtual ~ParameterInfluence() = default;
};

// Pressure-based influence (e.g., size increases with pressure)
struct PressureInfluence : public ParameterInfluence {
    PressureInfluence() { source = InfluenceSource::Pressure; }
    float evaluate(const StrokePoint& point, float random = 0.5f) const override;
};

// Speed-based influence (e.g., opacity decreases at slow speeds)
struct SpeedInfluence : public ParameterInfluence {
    float minSpeed = 0.1f;
    float maxSpeed = 5.0f;
    SpeedInfluence() { source = InfluenceSource::Speed; }
    float evaluate(const StrokePoint& point, float random = 0.5f) const override;
};

// Random jitter
struct RandomInfluence : public ParameterInfluence {
    RandomInfluence() { source = InfluenceSource::Random; }
    float evaluate(const StrokePoint& point, float random = 0.5f) const override;
};
