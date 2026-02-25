#pragma once

#include "StrokePoint.h"
#include <vector>
#include <deque>

// Smooths input path through exponential moving average
class PathStabilizer {
public:
    PathStabilizer(float stabilization = 0.5f);

    // Add raw input point, returns stabilized point
    StrokePoint addPoint(const StrokePoint& rawPoint);

    void setStabilization(float value) { m_stabilization = value; }
    float getStabilization() const { return m_stabilization; }

    void reset();

private:
    float m_stabilization;  // 0.0 (no smoothing) to 1.0 (max smoothing)
    std::deque<StrokePoint> m_history;
    sf::Vector2f m_smoothedPos;
};
