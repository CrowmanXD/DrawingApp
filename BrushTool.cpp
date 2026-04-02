#include "BrushTool.h"
#include "Canvas.h"

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

BrushTool::BrushTool()
    : m_color(sf::Color::Black),
    m_size(5.f),
    m_softness(12.0f),
    m_isDrawing(false) {

    // Initialize brush engine components
    m_stabilizer = std::make_unique<PathStabilizer>(0.5f);
    m_dabScheduler = std::make_unique<DabScheduler>(0.5f);
    m_dynamics = std::make_unique<BrushDynamics>();
    m_compositor = std::make_unique<DabCompositor>();

    // Keep default jitter subtle; high jitter makes spacing artifacts visible.
    m_dabScheduler->setJitter(0.05f);

    // Configure dynamics with default influences
    m_dynamics->setBaseSize(1.0f);
    m_dynamics->setBaseOpacity(1.0f);
    m_dynamics->setBaseFlow(1.0f);

    // Add pressure influence to size (pressure increases brush size)
    m_dynamics->addSizeInfluence(std::make_shared<PressureInfluence>());

    // Higher flow for better coverage with soft brush
    m_dynamics->setBaseFlow(0.45f);

    // Initialize spacing from current size.
    setSize(m_size);

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
    m_dabScheduler->setMinDistance(std::max(0.5f, size * 0.10f));
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

    // Stamp immediately so stroke heads are solid and not "beaded".
    Dab firstDab(position);
    m_dynamics->evaluateDab(firstDab, m_lastPoint, m_color);
    m_compositor->paintDab(canvas, firstDab, m_brushTexture, m_size, getCurrentBlendMode(canvas));
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
    // Generate a radial brush mask directly in an image.
    constexpr unsigned textureSize = 64;
    const float center = static_cast<float>(textureSize - 1) * 0.5f;
    const float maxRadius = static_cast<float>(textureSize) * 0.5f;

    sf::Image image;

    // FIX 1: Initialize the texture to Transparent Black!
    image.resize({ textureSize, textureSize }, sf::Color(0, 0, 0, 0));

    const float hardness = 30.0f / m_softness;

    for (unsigned y = 0; y < textureSize; ++y) {
        for (unsigned x = 0; x < textureSize; ++x) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float normalized = std::clamp(dist / maxRadius, 0.0f, 1.0f);

            // Soft edge profile with adjustable hardness.
            const float t = std::pow(normalized, hardness);
            const float alpha = 1.0f - std::clamp(t, 0.0f, 1.0f);

            // FIX 2: Premultiply the RGB channels by the alpha!
            std::uint8_t a = static_cast<std::uint8_t>(alpha * 255.0f);
            image.setPixel({ x, y }, sf::Color(a, a, a, a));
        }
    }

    m_brushTexture.loadFromImage(image);
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
    m_compositor->paintDabs(canvas, dabs, m_brushTexture, m_size, getCurrentBlendMode(canvas));

    m_lastPoint = stabilizedPoint;
}

sf::BlendMode BrushTool::getCurrentBlendMode(Canvas& canvas) const {
    bool alphaLocked = false;
    int activeIdx = canvas.getActiveLayerIndex();

    if (activeIdx >= 0 && activeIdx < canvas.getLayers().size()) {
        alphaLocked = canvas.getLayers()[activeIdx]->alphaLocked;
    }

    if (m_isEraser) {
        if (alphaLocked) {
            return sf::BlendMode(
                sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add
            );
        }
        return sf::BlendMode(
            sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
            sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
        );
    }

    if (alphaLocked) {
        // ALPHA LOCK BLEND MODE
        return sf::BlendMode(
            sf::BlendMode::Factor::DstAlpha, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
            sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add
        );
    }

    // NORMAL PREMULTIPLIED BLEND MODE
    // FIX 3: Change Color Source Factor to `One` because the texture is natively premultiplied!
    return sf::BlendMode(
        sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
        sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
    );
}
