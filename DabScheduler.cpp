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

    float dx = currentPoint.position.x - lastPoint.position.x;
    float dy = currentPoint.position.y - lastPoint.position.y;
    float segmentLength = std::sqrt(dx * dx + dy * dy);

    float spacing = std::max(0.01f, m_minDistance);

    // Always accumulate the distance
    m_accumulatedDistance += segmentLength;

    // If not enough distance to drop a dab yet, wait for the next frame.
    if (m_accumulatedDistance < spacing) {
        return dabs;
    }

    float dirX = 1.0f;
    float dirY = 0.0f;
    if (segmentLength > 0.0001f) {
        dirX = dx / segmentLength;
        dirY = dy / segmentLength;
    }
    float perpX = -dirY;
    float perpY = dirX;

    // Trace backward mathematically to find exactly where the first dab should have spawned
    float leftoverFromLastFrame = m_accumulatedDistance - segmentLength;
    float distIntoSegment = spacing - leftoverFromLastFrame;

    // Step along the segment and drop dabs
    while (distIntoSegment <= segmentLength) {
        float t = 0.0f;
        if (segmentLength > 0.0001f) {
            t = distIntoSegment / segmentLength;
        }

        Dab dab;
        float baseX = lastPoint.position.x + dx * t;
        float baseY = lastPoint.position.y + dy * t;

        float jitterPixels = m_jitter * spacing * 0.35f;
        float perpendicularOffset = (m_randomDist(m_rng) - 0.5f) * 2.0f * jitterPixels;
        float forwardOffset = (m_randomDist(m_rng) - 0.5f) * 0.3f * jitterPixels;

        dab.position = sf::Vector2f(
            baseX + perpX * perpendicularOffset + dirX * forwardOffset,
            baseY + perpY * perpendicularOffset + dirY * forwardOffset
        );

        dab.size = 1.0f;
        dab.opacity = 1.0f;
        dab.flow = 1.0f;
        dabs.push_back(dab);

        distIntoSegment += spacing;
    }

    // Carry the exact mathematical remainder into the next frame
    m_accumulatedDistance = segmentLength - (distIntoSegment - spacing);

    return dabs;
}

void DabScheduler::reset() {
    m_accumulatedDistance = 0.0f;
}
