#include "DabScheduler.h"
#include <cmath>
#include <chrono>
#include <algorithm>

DabScheduler::DabScheduler(float spacing)
    : m_spacing(spacing), m_minDistance(1.0f), m_jitter(0.5f), m_accumulatedDistance(0.0f),
      m_rng(std::chrono::system_clock::now().time_since_epoch().count()),
      m_randomDist(0.0f, 1.0f) {
}

std::vector<Dab> DabScheduler::scheduleDabs(const StrokePoint& currentPoint, const StrokePoint& lastPoint) {
    std::vector<Dab> dabs;

    const float dx = currentPoint.position.x - lastPoint.position.x;
    const float dy = currentPoint.position.y - lastPoint.position.y;
    const float segmentLength = std::sqrt(dx * dx + dy * dy);

    if (segmentLength < 0.001f) {
        return dabs;
    }

    const float spacing = std::max(0.01f, m_minDistance);

    const float dirX = dx / segmentLength;
    const float dirY = dy / segmentLength;
    const float perpX = -dirY;
    const float perpY = dirX;

    float progressed = 0.0f;
    float distanceToNextDab = spacing - m_accumulatedDistance;
    if (distanceToNextDab <= 0.0f) {
        distanceToNextDab = spacing;
    }

    while (progressed + distanceToNextDab <= segmentLength) {
        progressed += distanceToNextDab;
        const float t = progressed / segmentLength;

        Dab dab;
        const float baseX = lastPoint.position.x + dx * t;
        const float baseY = lastPoint.position.y + dy * t;

        // Keep jitter subtle and relative to spacing to avoid visible bead artifacts.
        const float jitterPixels = m_jitter * spacing * 0.35f;
        const float perpendicularOffset = (m_randomDist(m_rng) - 0.5f) * 2.0f * jitterPixels;
        const float forwardOffset = (m_randomDist(m_rng) - 0.5f) * 0.3f * jitterPixels;

        dab.position = sf::Vector2f(
            baseX + perpX * perpendicularOffset + dirX * forwardOffset,
            baseY + perpY * perpendicularOffset + dirY * forwardOffset
        );

        dab.size = 1.0f;
        dab.opacity = 1.0f;
        dab.flow = 1.0f;
        dabs.push_back(dab);

        distanceToNextDab = spacing;
    }

    // Carry distance remainder into next segment (critical for consistent dab cadence).
    m_accumulatedDistance = segmentLength - progressed;

    return dabs;
}

void DabScheduler::reset() {
    m_accumulatedDistance = 0.0f;
}
