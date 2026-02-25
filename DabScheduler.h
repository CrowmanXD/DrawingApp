#pragma once

#include "StrokePoint.h"
#include "Dab.h"
#include <vector>
#include <random>

// Determines where brush dabs are placed based on spacing rules
class DabScheduler {
public:
    DabScheduler(float spacing = 1.0f);

    // Add point and get list of dabs to paint
    std::vector<Dab> scheduleDabs(const StrokePoint& currentPoint, const StrokePoint& lastPoint);

    void setSpacing(float spacing) { m_spacing = spacing; }  // in pixels, relative to brush size
    void setMinDistance(float minDist) { m_minDistance = minDist; }
    void setJitter(float jitter) { m_jitter = jitter; }  // 0.0 = no jitter, 1.0 = max jitter
    float getJitter() const { return m_jitter; }

    void reset();

private:
    float m_spacing;        // dabs per brush size (e.g., 0.5 = dab every 50% of brush width)
    float m_minDistance;    // minimum pixels between dabs
    float m_jitter;         // random offset amount
    float m_accumulatedDistance;

    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_randomDist;

    // Interpolate dabs between two points
    std::vector<sf::Vector2f> interpolateDabPositions(
        const sf::Vector2f& from,
        const sf::Vector2f& to
    );
};
