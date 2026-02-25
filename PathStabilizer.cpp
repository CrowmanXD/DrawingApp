#include "PathStabilizer.h"

PathStabilizer::PathStabilizer(float stabilization)
    : m_stabilization(stabilization), m_smoothedPos(0.f, 0.f) {
}

StrokePoint PathStabilizer::addPoint(const StrokePoint& rawPoint) {
    // On first point, initialize smoothed position to avoid blending with (0,0)
    if (m_history.empty()) {
        m_smoothedPos = rawPoint.position;
    }

    m_history.push_back(rawPoint);

    // Keep only recent history for efficiency
    if (m_history.size() > 10) {
        m_history.pop_front();
    }

    // Smooth position with exponential moving average
    float alpha = 1.0f - m_stabilization;  // Higher stabilization = lower alpha
    m_smoothedPos = sf::Vector2f(
        m_smoothedPos.x * m_stabilization + rawPoint.position.x * alpha,
        m_smoothedPos.y * m_stabilization + rawPoint.position.y * alpha
    );

    // Return point with smoothed position but raw pressure/tilt
    StrokePoint stabilized = rawPoint;
    stabilized.position = m_smoothedPos;
    return stabilized;
}

void PathStabilizer::reset() {
    m_history.clear();
    m_smoothedPos = sf::Vector2f(0.f, 0.f);
}
