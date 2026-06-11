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

    void setPenPressure(float pressure) override { m_currentPressure = pressure; }

    void setEraser(bool isEraser) { m_isEraser = isEraser; }
    bool isEraser() const { return m_isEraser; }

    void onMouseDown(Canvas& canvas, sf::Vector2f position) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f position) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f position) override;

protected: // (Protected so the Eraser/Pen child classes can see it)
    float m_currentPressure = 1.0f;

private:
    // Create soft brush texture
    void createBrushTexture();

    // Process raw input and emit dabs
    void processInputPoint(Canvas& canvas, sf::Vector2f position, float pressure = 1.0f);

    sf::BlendMode getCurrentBlendMode(Canvas& canvas) const;

    // Brush engine components
    std::unique_ptr<PathStabilizer> m_stabilizer;
    std::unique_ptr<DabScheduler> m_dabScheduler;
    std::unique_ptr<BrushDynamics> m_dynamics;
    std::unique_ptr<DabCompositor> m_compositor;

    // Brush data
    sf::Color m_color;
    float m_size;
    float m_softness;
    sf::Texture m_brushTexture;
    bool m_textureCreated = false;

    // Stroke tracking
    StrokePoint m_lastPoint;
    bool m_isDrawing = false;
    bool m_isEraser = false;

    std::vector<StrokePoint> m_pointBuffer;
};