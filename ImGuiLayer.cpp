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

    // Brush settings panel
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Brush Settings");

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

    ImGui::End();
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
