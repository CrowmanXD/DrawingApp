#include "BrushTool.h"
#include "Canvas.h"

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

static sf::Vector2f getCatmullRomPosition(float t, sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3) {
    float t2 = t * t;
    float t3 = t2 * t;

    float x = 0.5f * ((2.0f * p1.x) +
        (-p0.x + p2.x) * t +
        (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
        (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);

    float y = 0.5f * ((2.0f * p1.y) +
        (-p0.y + p2.y) * t +
        (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);

    return sf::Vector2f(x, y);
}

BrushTool::BrushTool()
    : m_color(sf::Color::Black),
    m_size(5.f),
    m_softness(0.5f),
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
    m_dabScheduler->setMinDistance(std::max(0.5f, size * 0.05f));
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
    m_lastPoint = StrokePoint(position, m_currentPressure);

    m_stabilizer->reset();
    m_dabScheduler->reset();

    m_pointBuffer.clear();
    // Push the first coordinate 3 times so the Catmull-Rom math 
    // has enough fake "history" anchors to start calculating immediately
    m_pointBuffer.push_back(m_lastPoint);
    m_pointBuffer.push_back(m_lastPoint);
    m_pointBuffer.push_back(m_lastPoint);

    // Stamp immediately so stroke heads are solid and not "beaded".
    Dab firstDab(position);
    m_dynamics->evaluateDab(firstDab, m_lastPoint, m_color);
    m_compositor->paintDab(canvas, firstDab, m_brushTexture, m_size, getCurrentBlendMode(canvas));
}

void BrushTool::onMouseMove(Canvas& canvas, sf::Vector2f position) {
    if (!m_isDrawing)
        return;

    processInputPoint(canvas, position, m_currentPressure);
}

void BrushTool::onMouseUp(Canvas& canvas, sf::Vector2f position) {
    m_isDrawing = false;

    // The curve math lags 1 point behind the cursor, final segment has to be forcefully drawn when the user lifts their pen
    if (m_pointBuffer.size() >= 4) {
        size_t sz = m_pointBuffer.size();
        StrokePoint p0 = m_pointBuffer[sz - 3];
        StrokePoint p1 = m_pointBuffer[sz - 2];
        StrokePoint p2 = m_pointBuffer[sz - 1];
        StrokePoint p3 = m_pointBuffer[sz - 1]; // Duplicate last point to cap off the curve safely

        // Dynamic Resolution
        float segDx = p2.position.x - p1.position.x;
        float segDy = p2.position.y - p1.position.y;
        float distance = std::sqrt(segDx * segDx + segDy * segDy);
        int numSteps = std::max(10, static_cast<int>(distance));

        StrokePoint lastSubPoint = p1;

        for (int i = 1; i <= numSteps; ++i) {
            float t = static_cast<float>(i) / numSteps;
            sf::Vector2f subPos = getCatmullRomPosition(t, p0.position, p1.position, p2.position, p3.position);
            StrokePoint subPoint(subPos, p1.pressure + (p2.pressure - p1.pressure) * t);

            // Dynamic Spacing
            Dab previewDab;
            m_dynamics->evaluateDab(previewDab, subPoint, m_color);
            float currentPixelSize = previewDab.size * m_size;
            m_dabScheduler->setMinDistance(std::max(0.5f, currentPixelSize * 0.05f));

            auto dabs = m_dabScheduler->scheduleDabs(subPoint, lastSubPoint);
            for (auto& dab : dabs) m_dynamics->evaluateDab(dab, subPoint, m_color);
            m_compositor->paintDabs(canvas, dabs, m_brushTexture, m_size, getCurrentBlendMode(canvas));

            lastSubPoint = subPoint;
        }
    }

    // Clear the buffer to free memory and prep for the next stroke
    m_pointBuffer.clear();
}

void BrushTool::createBrushTexture() {
    // Generate a radial brush mask directly in an image.
    constexpr unsigned textureSize = 64;
    const float center = static_cast<float>(textureSize - 1) * 0.5f;
    const float maxRadius = static_cast<float>(textureSize) * 0.5f;

    sf::Image image;

    // Initialize the texture to Transparent Black
    image.resize({ textureSize, textureSize }, sf::Color(0, 0, 0, 0));

    const float hardness = 30.0f / m_softness;

    for (unsigned y = 0; y < textureSize; ++y) {
        for (unsigned x = 0; x < textureSize; ++x) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            const float dist = std::sqrt(dx * dx + dy * dy);

            float alpha = 0.0f;

            if (dist > maxRadius) {
                // Outside the brush
                alpha = 0.0f;
            }
            else if (m_softness < 0.01f) {
                // PURE HARD BRUSH
                // Add exactly 1 pixel of anti-aliasing to the edge so it isn't pixelated
                alpha = (dist > maxRadius - 1.0f) ? (maxRadius - dist) : 1.0f;
            }
            else {
                // AIRBRUSH / SOFT BRUSH
                // Calculate how big the solid core is before it starts fading
                float innerRadius = maxRadius * (1.0f - m_softness);

                if (dist <= innerRadius) {
                    alpha = 1.0f; // Inside the solid core
                }
                else {
                    // Inside the falloff zone: Calculate percentage from inner to outer radius
                    float t = (dist - innerRadius) / (maxRadius - innerRadius);

                    // Apply a Smoothstep curve
                    alpha = std::pow(1.0f - t, 3.0f);
                }
            }

            // Premultiply the RGB channels by the alpha
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

    // Add the newest point to our buffer
    m_pointBuffer.push_back(stabilizedPoint);

    // Schedule dabs based on stabilized point
    auto dabs = m_dabScheduler->scheduleDabs(stabilizedPoint, m_lastPoint);

    // Need 4 points to draw a curve between p1 and p2.
    // (This means the stroke visually trails the cursor by exactly 1 event, which makes the curve possible)
    if (m_pointBuffer.size() >= 4) {
        size_t sz = m_pointBuffer.size();
        StrokePoint p0 = m_pointBuffer[sz - 4];
        StrokePoint p1 = m_pointBuffer[sz - 3];
        StrokePoint p2 = m_pointBuffer[sz - 2];
        StrokePoint p3 = m_pointBuffer[sz - 1]; // The point your mouse just hit

		// Dymanic resolution: Calculate how many steps we need to take between p1 and p2 based on the physical distance between them
        float segDx = p2.position.x - p1.position.x;
        float segDy = p2.position.y - p1.position.y;
        float distance = std::sqrt(segDx * segDx + segDy * segDy);

        // Guarantee at least 10 steps, but otherwise mandate 1 step per physical pixel
        int numSteps = std::max(10, static_cast<int>(distance));
        StrokePoint lastSubPoint = p1;

        for (int i = 1; i <= numSteps; ++i) {
            float t = static_cast<float>(i) / numSteps;
            sf::Vector2f subPos = getCatmullRomPosition(t, p0.position, p1.position, p2.position, p3.position);

            StrokePoint subPoint(subPos, p1.pressure + (p2.pressure - p1.pressure) * t);
            subPoint.speed = p1.speed + (p2.speed - p1.speed) * t;

            // Dynamic spacing
            // Ask the Dynamics engine exactly how big the brush will be before we schedule the dabs
            Dab previewDab;
            m_dynamics->evaluateDab(previewDab, subPoint, m_color);
            float currentPixelSize = previewDab.size * m_size;

            // Update the scheduler so the gap shrinks alongside the brush
            m_dabScheduler->setMinDistance(std::max(0.5f, currentPixelSize * 0.05f));
            auto dabs = m_dabScheduler->scheduleDabs(subPoint, lastSubPoint);

            for (auto& dab : dabs) {
                m_dynamics->evaluateDab(dab, subPoint, m_color);
            }
            m_compositor->paintDabs(canvas, dabs, m_brushTexture, m_size, getCurrentBlendMode(canvas));

            lastSubPoint = subPoint;
        }
    }

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
    return sf::BlendMode(
        sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
        sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
    );
}
