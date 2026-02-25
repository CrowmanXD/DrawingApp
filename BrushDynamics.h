#pragma once

#include "Dab.h"
#include "StrokePoint.h"
#include "ParameterInfluence.h"
#include <vector>
#include <memory>
#include <random>

// Evaluates final dab parameters based on all influences
class BrushDynamics {
public:
    BrushDynamics();
    
    // Base parameters
    void setBaseSize(float size) { m_baseSize = size; }
    float getBaseSize() const { return m_baseSize; }
    void setBaseOpacity(float opacity) { m_baseOpacity = opacity; }
    float getBaseOpacity() const { return m_baseOpacity; }
    void setBaseFlow(float flow) { m_baseFlow = flow; }
    float getBaseFlow() const { return m_baseFlow; }
    
    // Add influences to parameters
    void addSizeInfluence(std::shared_ptr<ParameterInfluence> influence) {
        m_sizeInfluences.push_back(influence);
    }
    void addOpacityInfluence(std::shared_ptr<ParameterInfluence> influence) {
        m_opacityInfluences.push_back(influence);
    }
    void addFlowInfluence(std::shared_ptr<ParameterInfluence> influence) {
        m_flowInfluences.push_back(influence);
    }
    
    // Evaluate dab with all influences applied
    void evaluateDab(Dab& dab, const StrokePoint& point, const sf::Color& baseColor);
    
    void reset();

private:
    // Base parameters
    float m_baseSize = 1.0f;
    float m_baseOpacity = 1.0f;
    float m_baseFlow = 1.0f;
    
    // Influences per parameter
    std::vector<std::shared_ptr<ParameterInfluence>> m_sizeInfluences;
    std::vector<std::shared_ptr<ParameterInfluence>> m_opacityInfluences;
    std::vector<std::shared_ptr<ParameterInfluence>> m_flowInfluences;
    
    // Random number generation
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_randomDist;
    
    float evaluateInfluences(const std::vector<std::shared_ptr<ParameterInfluence>>& influences,
                            const StrokePoint& point);
};
