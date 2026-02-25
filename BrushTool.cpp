#include "BrushTool.h"
#include "Canvas.h"

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/CircleShape.hpp>
#include <cmath>

BrushTool::BrushTool()
    : m_color(sf::Color::Black),
    m_size(5.f),
    m_softness(12.0f),  // VERY soft - almost Gaussian
    m_isDrawing(false) {

    // Initialize brush engine components
    m_stabilizer = std::make_unique<PathStabilizer>(0.5f);
    m_dabScheduler = std::make_unique<DabScheduler>(0.5f);
    m_dynamics = std::make_unique<BrushDynamics>();
    m_compositor = std::make_unique<DabCompositor>();

    // Much higher jitter to break spacing regularity
    m_dabScheduler->setJitter(2.0f);

    // Configure dynamics with default influences
    m_dynamics->setBaseSize(1.0f);
    m_dynamics->setBaseOpacity(1.0f);
    m_dynamics->setBaseFlow(1.0f);

    // Add pressure influence to size (pressure increases brush size)
    m_dynamics->addSizeInfluence(std::make_shared<PressureInfluence>());

    // Add speed influence to opacity (slow strokes are more opaque)
    auto speedInfluence = std::make_shared<SpeedInfluence>();
    speedInfluence->strength = 0.3f;  // Moderate influence
    m_dynamics->addOpacityInfluence(speedInfluence);

    // Higher flow for better coverage with soft brush
    m_dynamics->setBaseFlow(0.45f);

    createBrushTexture();
}

void BrushTool::setColor(const sf::Color& color) {
    m_color = color;
}

sf::Color BrushTool::getColor() const {
    return m_color;
}

void BrushTool::setSize(float size) {
    m_size = size;
    // Set dab spacing to 2% of brush size for ULTRA dense coverage
    // This ensures dabs overlap so much that individual stamps disappear
    m_dabScheduler->setMinDistance(std::max(0.1f, size * 0.02f));
}

float BrushTool::getSize() const {
    return m_size;
}

void BrushTool::setSmoothing(float smoothing) {
    m_stabilizer->setStabilization(smoothing);
}

float BrushTool::getSmoothing() const {
    return m_stabilizer->getStabilization();
}

void BrushTool::setJitter(float jitter) {
    m_dabScheduler->setJitter(jitter);
}

float BrushTool::getJitter() const {
    return m_dabScheduler->getJitter();
}

void BrushTool::setFlow(float flow) {
    m_dynamics->setBaseFlow(flow);
}

float BrushTool::getFlow() const {
    return m_dynamics->getBaseFlow();
}

void BrushTool::setSoftness(float softness) {
    m_softness = softness;
    // Regenerate brush texture with new softness
    createBrushTexture();
}

float BrushTool::getSoftness() const {
    return m_softness;
}

void BrushTool::onMouseDown(Canvas& canvas, sf::Vector2f position) {
    m_isDrawing = true;
    m_lastPoint = StrokePoint(position, 1.0f);

    m_stabilizer->reset();
    m_dabScheduler->reset();
}

void BrushTool::onMouseMove(Canvas& canvas, sf::Vector2f position) {
    if (!m_isDrawing)
        return;

    processInputPoint(canvas, position, 1.0f);
}

void BrushTool::onMouseUp(Canvas& canvas, sf::Vector2f position) {
    m_isDrawing = false;
}

void BrushTool::createBrushTexture() {
    // Create a soft circular brush texture with feathered edges
    unsigned textureSize = 64;

    sf::RenderTexture rtex;
    rtex.resize(sf::Vector2u(textureSize, textureSize));
    rtex.clear(sf::Color::Transparent);

    float center = textureSize / 2.f;
    float maxRadius = textureSize / 2.f;

    // Draw soft circle using variable falloff based on softness setting
    // Softness controls the exponent: 1.0 = linear (hard), 7.0 = very soft
    for (int r = static_cast<int>(maxRadius); r > 0; --r) {
        float normalizedRadius = static_cast<float>(r) / maxRadius;

        // Apply power function with softness as exponent
        // Higher softness = softer edges
        float opacity = 1.f;
        for (int i = 0; i < static_cast<int>(m_softness); ++i) {
            opacity *= normalizedRadius;
        }
        opacity = 1.f - opacity;

        std::uint8_t alpha = static_cast<std::uint8_t>(opacity * 255.f);

        sf::CircleShape circle(static_cast<float>(r));
        circle.setFillColor(sf::Color(255, 255, 255, alpha));
        circle.setOrigin(sf::Vector2f(static_cast<float>(r), static_cast<float>(r)));
        circle.setPosition(sf::Vector2f(center, center));

        rtex.draw(circle);
    }

    rtex.display();
    m_brushTexture = rtex.getTexture();
    m_brushTexture.setSmooth(true);
    m_textureCreated = true;
}

void BrushTool::processInputPoint(Canvas& canvas, sf::Vector2f position, float pressure) {
    // Create raw input point
    StrokePoint rawPoint(position, pressure);

    // Calculate speed
    float dx = position.x - m_lastPoint.position.x;
    float dy = position.y - m_lastPoint.position.y;
    rawPoint.speed = std::sqrt(dx * dx + dy * dy);

    // Stabilize path
    StrokePoint stabilizedPoint = m_stabilizer->addPoint(rawPoint);

    // Schedule dabs based on stabilized point
    auto dabs = m_dabScheduler->scheduleDabs(stabilizedPoint, m_lastPoint);

    // Evaluate dabs with dynamics
    for (auto& dab : dabs) {
        m_dynamics->evaluateDab(dab, stabilizedPoint, m_color);
    }

    // Paint dabs to canvas
    m_compositor->paintDabs(canvas, dabs, m_brushTexture, m_size);

    m_lastPoint = stabilizedPoint;
}
