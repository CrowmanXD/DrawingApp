#pragma once

#include <SFML/Graphics.hpp>
#include "ImGuiLayer.h"
#include "Canvas.h"
#include "Tool.h"
#include "BrushTool.h"
#include "AppState.h"
#include "RectSelectTool.h"
#include "SelectionBrushTool.h"
#include "VectorPenTool.h"
#include "FreehandSelectTool.h"
#include "PaintBucketTool.h"
#include "ShapeTool.h"
#include "CropTool.h"
#include "TransformTool.h"
#include "ColorPickerTool.h"
#include "AssistantController.h"
#include <memory>

struct Shortcut {
    sf::Keyboard::Key key = sf::Keyboard::Key::Unknown;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

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
    void setShapeType(int type);
    int getShapeType() const;
    bool isShapeFilled() const;
    void setShapeFilled(bool filled);
    float getWorkspaceRotation() const;
    void setWorkspaceRotation(float rot);
    float getPenPressure() const;
    void setPenPressure(float p);

    float getPressureCurve() const { return m_pressureCurve; }
    void setPressureCurve(float curve) { m_pressureCurve = curve; }

    // --- SHORTCUT MANAGEMENT ---
    bool isShortcutPressed(const std::string& action, const sf::Event::KeyPressed* keyEvent);
    std::string getShortcutString(const std::string& action);
    std::string getKeyName(sf::Keyboard::Key key);
    std::map<std::string, Shortcut>& getShortcuts() { return m_shortcuts; }
    void setActionToRebind(const std::string& action) { m_actionToRebind = action; }
    std::string getActionToRebind() const { return m_actionToRebind; }
    void restoreDefaultShortcuts();

    void loadProject(const std::string& filepath);
    bool saveCurrentProject();
    bool saveProjectAs();
    void loadSettings();
    void saveSettings();
    std::string getApiDomain() const;
    void setApiDomain(const std::string& domain);
    void initiateExit();
    void forceExit();
    void cancelExit();
    bool shouldShowExitWarning() const { return m_showExitWarning; }

    int getTheme() const { return m_theme; }
    void setTheme(int theme) { m_theme = theme; }

    sf::Vector2f getCanvasOffset() const;
    sf::Vector2u getWindowSize() const;
    Canvas& getCanvas() const;

    // --- AI ASSISTANT ---
    bool isAssistantEnabled() const { return m_assistantEnabled; }
    void setAssistantEnabled(bool enabled) { m_assistantEnabled = enabled; }

    AssistantController* getAssistant() const { return m_assistant.get(); }

private:
    void processEvents();
    void update(sf::Time deltaTime);
    void render();
    void initShortcuts();
    std::map<std::string, Shortcut> m_shortcuts;
    std::string m_actionToRebind = "";
    std::string m_currentFilePath = "";
    bool m_showExitWarning = false;
    std::string m_apiDomain = "fallback.trycloudflare.com";

    float m_zoom = 1.0f;
    sf::Vector2f m_pan = { 0.f, 0.f };
    bool m_isPanning = false;
    sf::Vector2i m_lastMousePos;
    int m_theme = 0; // 0 = Dark, 1 = Light, 2 = Classic
    float m_workspaceRotation = 0.f;
    float m_penPressure = 1.0f; // 1.0 = max pressure
    float m_pressureCurve = 1.0f; // 1.0 = Linear, < 1.0 = Soft, > 1.0 = Hard

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
    VectorPenTool m_penTool;
    FreehandSelectTool m_freehandSelectTool;
    PaintBucketTool m_paintBucketTool;
    ShapeTool m_shapeTool;
    CropTool m_cropTool;
    TransformTool m_transformTool;
	ColorPickerTool m_colorPickerTool;
    Tool* m_activeTool = nullptr;
    int m_currentToolMode = 0; // 0 = Brush, 1 = Select

    std::unique_ptr<AssistantController> m_assistant;
    bool m_assistantEnabled = false;
};

