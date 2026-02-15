#pragma once

#include <SFML/Graphics.hpp>
#include "ImGuiLayer.h"
#include "Canvas.h"
#include <memory>
#include "Tool.h"
#include "BrushTool.h"

class Application {
public:
    Application();
    ~Application();

    void run();

private:
    void processEvents();
    void update(sf::Time deltaTime);
    void render();

private:
    sf::RenderWindow m_window;
    bool m_running = true;
    ImGuiLayer m_imgui;
    Canvas m_canvas;
    std::unique_ptr<Tool> m_activeTool;
};
