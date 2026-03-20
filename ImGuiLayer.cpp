#include "ImGuiLayer.h"
#include "Application.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include <cstdint>

ImGuiLayer::ImGuiLayer() = default;
ImGuiLayer::~ImGuiLayer() = default;

void ImGuiLayer::init(sf::RenderWindow& window) {
    ImGui::SFML::Init(window);
}

void ImGuiLayer::processEvent(sf::RenderWindow& window, const sf::Event& event) {
    ImGui::SFML::ProcessEvent(window, event);
}

void ImGuiLayer::update(sf::RenderWindow& window, sf::Time deltaTime, Application& app) {
    ImGui::SFML::Update(window, deltaTime);

    if (app.getState() == AppState::StartupScreen) {
        // Center the window on the screen
        ImVec2 center(window.getSize().x * 0.5f, window.getSize().y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 250), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

        ImGui::Begin("Create New Canvas", nullptr, flags);

        // We need static variables to hold the user's input before they click create
        static int canvasWidth = 1920;
        static int canvasHeight = 1080;

        ImGui::Text("Enter desired canvas dimensions:");
        ImGui::Spacing();

        ImGui::InputInt("Width", &canvasWidth);
        ImGui::InputInt("Height", &canvasHeight);

        // Clamp values so they don't crash the app by making a 0x0 texture
        if (canvasWidth < 100) canvasWidth = 100;
        if (canvasHeight < 100) canvasHeight = 100;

        ImGui::Spacing();
        ImGui::Spacing();

        // Centered Create Button
        float windowWidth = ImGui::GetWindowSize().x;
        float textWidth = ImGui::CalcTextSize("Create Canvas").x;
        ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);

        if (ImGui::Button("Create Canvas", ImVec2(150, 40))) {
            app.startDrawing(static_cast<unsigned int>(canvasWidth), static_cast<unsigned int>(canvasHeight));
        }

        ImGui::End();
	}
    else if (app.getState() == AppState::DrawingEditor) {
        // --- BRUSH SETTINGS SIDEBAR ---

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.f, static_cast<float>(app.getWindowSize().y)), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoTitleBar;

        ImGui::Begin("Brush Settings", nullptr, flags);
        ImGui::PushItemWidth(-130.0f);

        // Color picker
        sf::Color currentColor = app.getBrushColor();
        float col[4] = {
            currentColor.r / 255.f,
            currentColor.g / 255.f,
            currentColor.b / 255.f,
            currentColor.a / 255.f
        };

        if (ImGui::ColorEdit4("Brush Color", col)) {
            sf::Color newColor(
                static_cast<std::uint8_t>(col[0] * 255.f),
                static_cast<std::uint8_t>(col[1] * 255.f),
                static_cast<std::uint8_t>(col[2] * 255.f),
                static_cast<std::uint8_t>(col[3] * 255.f)
            );
            app.setBrushColor(newColor);
        }

        bool isEraser = app.isEraser();
        if (ImGui::Checkbox("Eraser Mode", &isEraser)) {
            app.setEraser(isEraser);
        }
        ImGui::Separator();

        // Brush size slider
        float size = app.getBrushSize();
        if (ImGui::SliderFloat("Brush Size", &size, 1.f, 50.f)) {
            app.setBrushSize(size);
        }

        // Path smoothing slider
        float smoothing = app.getBrushSmoothing();
        if (ImGui::SliderFloat("Path Smoothing", &smoothing, 0.f, 0.999f)) {
            app.setBrushSmoothing(smoothing);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.0 = No smoothing (raw input)\n1.0 = Maximum smoothing (lag)");
        }

        // Flow slider - KEY TO SMOOTH STROKES!
        float flow = app.getBrushFlow();
        if (ImGui::SliderFloat("Paint Flow", &flow, 0.1f, 1.0f)) {
            app.setBrushFlow(flow);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.1 = Very transparent (smooth blending)\n1.0 = Fully opaque (harsh circles)");
        }

        // Brush softness slider
        float softness = app.getBrushSoftness();
        if (ImGui::SliderFloat("Brush Softness", &softness, 1.0f, 15.0f)) {
            app.setBrushSoftness(softness);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("1.0 = Hard edges (sharp)\n7.0 = Very soft (default)\n15.0 = Ultra soft (Gaussian-like)");
        }

        ImGui::PopItemWidth();

        // --- LAYERS PANEL ---
        ImGui::Separator();
        ImGui::Text("Layers");
        ImGui::Spacing();

        if (ImGui::Button("Add New Layer", ImVec2(-1, 0))) {
            app.getCanvas().addLayer(); // Note: You'll need to add Canvas& getCanvas() to Application.h!
        }

        ImGui::BeginChild("LayerList", ImVec2(0, 150), true);

        auto& layers = app.getCanvas().getLayers();
        // Loop backwards so the top layer is at the top of the UI list
        for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
            ImGui::PushID(i);

            // Visibility Checkbox
            ImGui::Checkbox("##vis", &layers[i]->visible);
            ImGui::SameLine();

            // Layer Selection
            bool isSelected = (app.getCanvas().getActiveLayerIndex() == i);
            if (ImGui::Selectable(layers[i]->name.c_str(), isSelected)) {
                app.getCanvas().setActiveLayer(i);
            }

            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::End();
    }
}

void ImGuiLayer::render(sf::RenderWindow& window) {
    ImGui::SFML::Render(window);
}

void ImGuiLayer::shutdown() {
    ImGui::SFML::Shutdown();
}

bool ImGuiLayer::wantsCaptureMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}
