#include "BrushDynamics.h"
#include <chrono>
#include <algorithm>
#include <cstdint>

BrushDynamics::BrushDynamics() 
    : m_rng(std::chrono::system_clock::now().time_since_epoch().count()),
      m_randomDist(0.0f, 1.0f) {
}

void BrushDynamics::evaluateDab(Dab& dab, const StrokePoint& point, const sf::Color& baseColor) {
    // Evaluate each parameter with its influences
    dab.size = m_baseSize * evaluateInfluences(m_sizeInfluences, point);
    dab.opacity = m_baseOpacity * evaluateInfluences(m_opacityInfluences, point);
    dab.flow = m_baseFlow * evaluateInfluences(m_flowInfluences, point);

    // Clamp to valid ranges
    dab.size = std::clamp(dab.size, 0.1f, 100.0f);
    dab.opacity = std::clamp(dab.opacity, 0.0f, 1.0f);
    dab.flow = std::clamp(dab.flow, 0.0f, 1.0f);

    // Apply color
    dab.color = baseColor;

    // Modulate alpha by opacity
    dab.color.a = static_cast<std::uint8_t>(baseColor.a * dab.opacity);
}

float BrushDynamics::evaluateInfluences(const std::vector<std::shared_ptr<ParameterInfluence>>& influences, const StrokePoint& point) {
    if (influences.empty()) return 1.0f;
    
    float result = 1.0f;
    float randomValue = m_randomDist(m_rng);
    
    // Multiplicative blending of influences
    for (const auto& influence : influences) {
        result *= influence->evaluate(point, randomValue);
    }
    
    return result;
}

void BrushDynamics::reset() {
    // Could reset internal state if needed
}
