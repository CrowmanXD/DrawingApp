#include "CropTool.h"
#include "Canvas.h"
#include "LayerUndoCommands.h"
#include <cmath>

void CropTool::onActivate(Canvas& canvas) {
    m_isActive = true;
    // Instantly wrap the crop box around the exact canvas boundaries
    m_cropRect.position = { 0.f, 0.f };
    m_cropRect.size = { static_cast<float>(canvas.getSize().x), static_cast<float>(canvas.getSize().y) };
}

void CropTool::onDeactivate(Canvas& canvas) {
    m_isActive = false;
    m_activeHandle = -1;
}

// Math helper to figure out exactly which handle the mouse is hovering over
int CropTool::getHoveredHandle(sf::Vector2f pos, float zoom) {
    float x = m_cropRect.position.x;
    float y = m_cropRect.position.y;
    float w = m_cropRect.size.x;
    float h = m_cropRect.size.y;

    sf::Vector2f pts[8] = {
        {x, y}, {x + w / 2.f, y}, {x + w, y},           // Top Left, Top Mid, Top Right
        {x + w, y + h / 2.f}, {x + w, y + h},           // Mid Right, Bottom Right
        {x + w / 2.f, y + h}, {x, y + h},               // Bottom Mid, Bottom Left
        {x, y + h / 2.f}                                // Mid Left
    };

    // The hit radius gets smaller if zoomed in, larger if zoomed out, so it always feels easy to click
    float hitRadius = 15.0f / zoom;

    for (int i = 0; i < 8; ++i) {
        float dx = pos.x - pts[i].x;
        float dy = pos.y - pts[i].y;
        if (std::sqrt(dx * dx + dy * dy) <= hitRadius) return i;
    }

    // Check if they clicked inside the box to move the entire thing
    if (m_cropRect.contains(pos)) return 8;

    return -1;
}

void CropTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isActive) return;

    // We assume zoom is 1.0f for the pure logic collision
    m_activeHandle = getHoveredHandle(pos, m_currentZoom);

    if (m_activeHandle != -1) {
        m_dragStartPos = pos;
        m_dragStartRect = m_cropRect;
    }
}

void CropTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    if (m_activeHandle == -1) return;

    sf::Vector2f delta = pos - m_dragStartPos;
    sf::FloatRect newRect = m_dragStartRect;

    // Route the math depending on which edge/corner is being dragged
    if (m_activeHandle == 0 || m_activeHandle == 1 || m_activeHandle == 2) { // Top Edge
        newRect.position.y += delta.y;
        newRect.size.y -= delta.y;
    }
    if (m_activeHandle == 4 || m_activeHandle == 5 || m_activeHandle == 6) { // Bottom Edge
        newRect.size.y += delta.y;
    }
    if (m_activeHandle == 0 || m_activeHandle == 6 || m_activeHandle == 7) { // Left Edge
        newRect.position.x += delta.x;
        newRect.size.x -= delta.x;
    }
    if (m_activeHandle == 2 || m_activeHandle == 3 || m_activeHandle == 4) { // Right Edge
        newRect.size.x += delta.x;
    }
    if (m_activeHandle == 8) { // Move entire box
        newRect.position += delta;
    }

    // Prevent the user from dragging the box into negative sizes
    float minSize = 20.f;
    if (newRect.size.x < minSize) {
        if (m_activeHandle == 0 || m_activeHandle == 6 || m_activeHandle == 7) newRect.position.x -= (minSize - newRect.size.x);
        newRect.size.x = minSize;
    }
    if (newRect.size.y < minSize) {
        if (m_activeHandle == 0 || m_activeHandle == 1 || m_activeHandle == 2) newRect.position.y -= (minSize - newRect.size.y);
        newRect.size.y = minSize;
    }

    m_cropRect = newRect;
}

void CropTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    m_activeHandle = -1;
}

void CropTool::onKeyPress(Canvas& canvas, sf::Keyboard::Key key) {
    if (key == sf::Keyboard::Key::Escape) {
        // Reset back to canvas size
        onActivate(canvas);
    }
    else if (key == sf::Keyboard::Key::Enter && m_isActive && m_activeHandle == -1) {

        // Convert FloatRect to IntRect for the Engine execution
        sf::IntRect finalCrop;
        finalCrop.position = { static_cast<int>(m_cropRect.position.x), static_cast<int>(m_cropRect.position.y) };
        finalCrop.size = { static_cast<int>(m_cropRect.size.x), static_cast<int>(m_cropRect.size.y) };

        if (finalCrop.size.x > 0 && finalCrop.size.y > 0) {

            // Backup State
            std::vector<std::unique_ptr<sf::Image>> oldImages;
            for (const auto& layer : canvas.getLayers()) {
                oldImages.push_back(std::make_unique<sf::Image>(layer->texture->getTexture().copyToImage()));
            }

            canvas.pushUndoCommand(std::make_unique<CropUndoCommand>(
                canvas.getSize(), finalCrop, std::move(oldImages)
            ));

            // Execute the resize
            canvas.applyCrop(finalCrop);

            // Re-initialize the tool bounds to the newly expanded canvas
            onActivate(canvas);
        }
    }
}

void CropTool::onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation) {
    m_currentZoom = zoom;
    
    if (!m_isActive) return;

    auto toScreenPos = [&](float x, float y) { return sf::Vector2f(x * zoom + offset.x, y * zoom + offset.y); };
    auto toScreenSize = [&](float w, float h) { return sf::Vector2f(w * zoom, h * zoom); };

    // Draw Infinite Theater Mode
    // This draws darkness everywhere except inside the crop box
    sf::Color dim(0, 0, 0, 150);
    float huge = 100000.f;

    sf::RectangleShape top(toScreenSize(huge * 2.f, m_cropRect.position.y + huge));
    top.setPosition(toScreenPos(-huge, -huge));
    top.setFillColor(dim);

    sf::RectangleShape bot(toScreenSize(huge * 2.f, huge));
    bot.setPosition(toScreenPos(-huge, m_cropRect.position.y + m_cropRect.size.y));
    bot.setFillColor(dim);

    sf::RectangleShape left(toScreenSize(huge + m_cropRect.position.x, m_cropRect.size.y));
    left.setPosition(toScreenPos(-huge, m_cropRect.position.y));
    left.setFillColor(dim);

    sf::RectangleShape right(toScreenSize(huge, m_cropRect.size.y));
    right.setPosition(toScreenPos(m_cropRect.position.x + m_cropRect.size.x, m_cropRect.position.y));
    right.setFillColor(dim);

    window.draw(top); window.draw(bot); window.draw(left); window.draw(right);

    // Draw Crop Outline and Grid
    sf::RectangleShape outline(toScreenSize(m_cropRect.size.x, m_cropRect.size.y));
    outline.setPosition(toScreenPos(m_cropRect.position.x, m_cropRect.position.y));
    outline.setFillColor(sf::Color(0, 0, 0, 0));
    outline.setOutlineColor(sf::Color::White);
    outline.setOutlineThickness(2.0f);
    window.draw(outline);

    // Draw Rule of Thirds
    sf::VertexArray lines(sf::PrimitiveType::Lines);
    float wThirds = outline.getSize().x;
    float hThirds = outline.getSize().y;
    sf::Vector2f pos = outline.getPosition();

    // Verticals
    lines.append(sf::Vertex(pos + sf::Vector2f(wThirds * 0.33f, 0), sf::Color(255, 255, 255, 150)));
    lines.append(sf::Vertex(pos + sf::Vector2f(wThirds * 0.33f, hThirds), sf::Color(255, 255, 255, 150)));
    lines.append(sf::Vertex(pos + sf::Vector2f(wThirds * 0.66f, 0), sf::Color(255, 255, 255, 150)));
    lines.append(sf::Vertex(pos + sf::Vector2f(wThirds * 0.66f, hThirds), sf::Color(255, 255, 255, 150)));

    // Horizontals
    lines.append(sf::Vertex(pos + sf::Vector2f(0, hThirds * 0.33f), sf::Color(255, 255, 255, 150)));
    lines.append(sf::Vertex(pos + sf::Vector2f(wThirds, hThirds * 0.33f), sf::Color(255, 255, 255, 150)));
    lines.append(sf::Vertex(pos + sf::Vector2f(0, hThirds * 0.66f), sf::Color(255, 255, 255, 150)));
    lines.append(sf::Vertex(pos + sf::Vector2f(wThirds, hThirds * 0.66f), sf::Color(255, 255, 255, 150)));

    window.draw(lines);

    // Draw Interactive Drag Handles
    float x = m_cropRect.position.x;
    float y = m_cropRect.position.y;
    float w = m_cropRect.size.x;
    float h = m_cropRect.size.y;

    sf::Vector2f pts[8] = {
        {x, y}, {x + w / 2.f, y}, {x + w, y},
        {x + w, y + h / 2.f}, {x + w, y + h},
        {x + w / 2.f, y + h}, {x, y + h}, {x, y + h / 2.f}
    };

    int hoverIdx = getHoveredHandle(sf::Vector2f(
        (sf::Mouse::getPosition(window).x - offset.x) / zoom,
        (sf::Mouse::getPosition(window).y - offset.y) / zoom
    ), zoom);

    for (int i = 0; i < 8; ++i) {
        sf::RectangleShape handle({ 10.f, 10.f }); // 10x10 squares
        handle.setOrigin({ 5.f, 5.f });
        handle.setPosition(toScreenPos(pts[i].x, pts[i].y));
        handle.setFillColor(sf::Color::White);

        // Highlight blue if mouse is over it
        if (i == hoverIdx) handle.setOutlineColor(sf::Color(0, 150, 255));
        else handle.setOutlineColor(sf::Color::Black);

        handle.setOutlineThickness(1.5f);
        window.draw(handle);
    }
}