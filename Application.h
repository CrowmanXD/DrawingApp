#pragma once

#include <SFML/Graphics.hpp>
#include "ImGuiLayer.h"
#include "Canvas.h"
#include "Tool.h"
#include "BrushTool.h"
#include "AppState.h"
#include "RectSelectTool.h"
#include "SelectionBrushTool.h"
#include <memory>

class Application {
public:
    Application();
    ~Application();

    void run();

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
    void setToolMode(int mode);
    int getToolMode() const;

    sf::Vector2f getCanvasOffset() const;
    sf::Vector2u getWindowSize() const;
    Canvas& getCanvas() const;

private:
    void processEvents();
    void update(sf::Time deltaTime);
    void render();

    float m_zoom = 1.0f;
    sf::Vector2f m_pan = { 0.f, 0.f };
    bool m_isPanning = false;
    sf::Vector2i m_lastMousePos;

    sf::Shader m_antsShader;
    sf::Clock m_antsClock;

    sf::RenderWindow m_window;
    bool m_running = true;
    ImGuiLayer m_imgui;
    AppState m_state = AppState::StartupScreen;
    std::unique_ptr<Canvas> m_canvas;
    BrushTool m_brushTool;
    RectSelectTool m_rectSelectTool;
    SelectionBrushTool m_selectionBrushTool;
    Tool* m_activeTool = nullptr;
    int m_currentToolMode = 0; // 0 = Brush, 1 = Select
};

