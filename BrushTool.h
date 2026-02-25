#pragma once

#include "Tool.h"
#include "StrokePoint.h"
#include "PathStabilizer.h"
#include "DabScheduler.h"
#include "BrushDynamics.h"
#include "DabCompositor.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <vector>
#include <memory>

// High-level brush tool using Krita-style architecture
class BrushTool : public Tool {
public:
    BrushTool();

    void setColor(const sf::Color& color);
    sf::Color getColor() const;

    void setSize(float size);
    float getSize() const;

    void setSmoothing(float smoothing);
    float getSmoothing() const;

    void setJitter(float jitter);
    float getJitter() const;

    void setFlow(float flow);
    float getFlow() const;

    void setSoftness(float softness);
    float getSoftness() const;

    void onMouseDown(Canvas& canvas, sf::Vector2f position) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f position) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f position) override;

private:
    // Create soft brush texture
    void createBrushTexture();

    // Process raw input and emit dabs
    void processInputPoint(Canvas& canvas, sf::Vector2f position, float pressure = 1.0f);

private:
    // Brush engine components
    std::unique_ptr<PathStabilizer> m_stabilizer;
    std::unique_ptr<DabScheduler> m_dabScheduler;
    std::unique_ptr<BrushDynamics> m_dynamics;
    std::unique_ptr<DabCompositor> m_compositor;

    // Brush data
    sf::Color m_color;
    float m_size;
    float m_softness;  // Controls brush falloff curve (1.0 = hard, 7.0 = soft)
    sf::Texture m_brushTexture;
    bool m_textureCreated = false;

    // Stroke tracking
    StrokePoint m_lastPoint;
    bool m_isDrawing = false;
};