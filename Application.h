#pragma once

#include <SFML/Graphics.hpp>
#include "ImGuiLayer.h"
#include "Canvas.h"
#include <memory>
#include "Tool.h"
#include "BrushTool.h"
#include "AppState.h"

class Application {
public:
    Application();
    ~Application();

    void run();

    // State management
    AppState getState() const;
    void startDrawing(unsigned int width, unsigned int height);

    sf::Color getBrushColor() const;
    void setBrushColor(const sf::Color& color);
    float getBrushSize() const;
    void setBrushSize(float size);
    float getBrushSmoothing() const;
    void setBrushSmoothing(float smoothing);
    float getBrushJitter() const;
    void setBrushJitter(float jitter);
    float getBrushFlow() const;
    void setBrushFlow(float flow);
    float getBrushSoftness() const;
    void setBrushSoftness(float softness);
    bool isEraser() const;
    void setEraser(bool isEraser);

    sf::Vector2f getCanvasOffset() const;
    sf::Vector2u getWindowSize() const;
    Canvas& getCanvas() const;

private:
    void processEvents();
    void update(sf::Time deltaTime);
    void render();

    sf::RenderWindow m_window;
    bool m_running = true;
    ImGuiLayer m_imgui;
    AppState m_state = AppState::StartupScreen;
    std::unique_ptr<Canvas> m_canvas;
    std::unique_ptr<BrushTool> m_activeTool;
};

