#pragma once
#include "Tool.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/System/Vector2.hpp>

// Forward declarations to keep the header lightweight
class Application;
class Canvas;

class ColorPickerTool : public Tool {
private:
    Application& m_app;
    sf::Image m_cachedImage;
    bool m_isDragging;

    // Helper function to handle the actual color extraction
    void pickColor(sf::Vector2f position);

public:
    ColorPickerTool(Application& app);

    void onMouseDown(Canvas& canvas, sf::Vector2f position) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f position) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f position) override;
};