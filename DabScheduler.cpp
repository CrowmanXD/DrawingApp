#include "DabScheduler.h"
#include <cmath>
#include <chrono>

DabScheduler::DabScheduler(float spacing)
    : m_spacing(spacing), m_minDistance(1.0f), m_jitter(0.5f), m_accumulatedDistance(0.0f),
      m_rng(std::chrono::system_clock::now().time_since_epoch().count()),
      m_randomDist(0.0f, 1.0f) {
}

std::vector<Dab> DabScheduler::scheduleDabs(const StrokePoint& currentPoint, const StrokePoint& lastPoint) {
    std::vector<Dab> dabs;

    // Calculate distance between points
    float dx = currentPoint.position.x - lastPoint.position.x;
    float dy = currentPoint.position.y - lastPoint.position.y;
    float distance = std::sqrt(dx * dx + dy * dy);

    m_accumulatedDistance += distance;

    // Generate dabs along the path at spacing intervals
    if (m_accumulatedDistance >= m_minDistance) {
        auto positions = interpolateDabPositions(lastPoint.position, currentPoint.position);

        // Calculate stroke direction for jitter
        float dirLen = std::sqrt(dx * dx + dy * dy);
        float dirX = (dirLen > 0.01f) ? dx / dirLen : 1.0f;
        float dirY = (dirLen > 0.01f) ? dy / dirLen : 0.0f;

        // Calculate perpendicular direction for natural offset
        float perpX = -dirY;
        float perpY = dirX;

        for (const auto& pos : positions) {
            Dab dab;

            // Apply perpendicular jitter (across stroke width)
            float perpendicularOffset = (m_randomDist(m_rng) - 0.5f) * 2.0f * m_jitter;

            // Apply FORWARD jitter (along stroke direction) to break regularity
            float forwardOffset = (m_randomDist(m_rng) - 0.5f) * m_jitter;

            dab.position = sf::Vector2f(
                pos.x + perpX * perpendicularOffset + dirX * forwardOffset,
                pos.y + perpY * perpendicularOffset + dirY * forwardOffset
            );

            // Add slight size variation
            float sizeVariation = 0.90f + (m_randomDist(m_rng) * 0.2f);

            // Add opacity jitter
            float opacityVariation = 0.90f + (m_randomDist(m_rng) * 0.2f);

            dab.size = sizeVariation;
            dab.opacity = opacityVariation;
            dab.flow = 1.0f;
            dabs.push_back(dab);
        }

        m_accumulatedDistance = 0.0f;
    }

    return dabs;
}

std::vector<sf::Vector2f> DabScheduler::interpolateDabPositions(
    const sf::Vector2f& from,
    const sf::Vector2f& to
) {
    std::vector<sf::Vector2f> positions;
    
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    if (distance < 0.01f) return positions;
    
    // Place dabs at m_minDistance intervals
    float steps = distance / m_minDistance;
    int numSteps = static_cast<int>(steps);
    
    for (int i = 0; i <= numSteps; ++i) {
        float t = (numSteps > 0) ? static_cast<float>(i) / numSteps : 0.0f;
        sf::Vector2f pos = from + sf::Vector2f(dx, dy) * t;
        positions.push_back(pos);
    }
    
    return positions;
}

void DabScheduler::reset() {
    m_accumulatedDistance = 0.0f;
}
