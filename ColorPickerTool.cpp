#include "ColorPickerTool.h"
#include "Application.h"
#include "Canvas.h"

ColorPickerTool::ColorPickerTool(Application& app)
    : m_app(app), m_isDragging(false) {
}

void ColorPickerTool::onMouseDown(Canvas& canvas, sf::Vector2f position) {
    // Download the flattened canvas from the GPU to the CPU.
    // Once per click so dragging the eyedropper is smooth
    m_cachedImage = canvas.getCompositeTexture().copyToImage();

    m_isDragging = true;
    pickColor(position);
}

void ColorPickerTool::onMouseMove(Canvas& canvas, sf::Vector2f position) {
    // Continue picking dynamically as the user drags the mouse
    if (m_isDragging) {
        pickColor(position);
    }
}

void ColorPickerTool::onMouseUp(Canvas& canvas, sf::Vector2f position) {
    m_isDragging = false;

    // Clear the cached image to free up RAM when they let go of the mouse
    m_cachedImage = sf::Image();
}

void ColorPickerTool::pickColor(sf::Vector2f position) {
    // Prevent out-of-bounds crashes if the user drags off the top/left of the canvas
    if (position.x < 0 || position.y < 0) return;

    unsigned int x = static_cast<unsigned int>(position.x);
    unsigned int y = static_cast<unsigned int>(position.y);

    // Prevent out-of-bounds crashes if the user drags off the bottom/right of the canvas
    if (x < m_cachedImage.getSize().x && y < m_cachedImage.getSize().y) {

        sf::Color picked = m_cachedImage.getPixel({ x, y });

        // Force the alpha to 255. 
        // Even if the user picks a semi-transparent stroke, we want the brush to equip the solid base color they clicked on
        picked.a = 255;

        m_app.setBrushColor(picked);
    }
}