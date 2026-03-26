#include "ImGuiLayer.h"
#include "Application.h"
#include "LayerUndoCommands.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include <cstdint>
#include <functional>
#include <vector>

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

        // Flow slider 
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

        if (ImGui::Button("Add Layer", ImVec2(130, 0))) { app.getCanvas().addLayer(); }
        ImGui::SameLine();
        if (ImGui::Button("Add Folder", ImVec2(130, 0))) { app.getCanvas().addFolder(); }

        ImGui::Spacing();

        // Get the active layer to connect to the slider
        auto& layers = app.getCanvas().getLayers();
        int activeIdx = app.getCanvas().getActiveLayerIndex();

        if (activeIdx >= 0 && activeIdx < layers.size()) {
            // --- BLEND MODE DROPDOWN ---
            const char* blendModeNames[] = { "Normal", "Multiply", "Add (Linear Dodge)", "Pass Through" };
            int currentBlendInt = static_cast<int>(layers[activeIdx]->blendMode);

            if (ImGui::Combo("Blend Mode", &currentBlendInt, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {

                // Content layers cannot be Pass Through, force them back to Normal.
                if (layers[activeIdx]->type == LayerType::Content && currentBlendInt == 3) {
                    currentBlendInt = 0;
                }

                app.getCanvas().pushUndoCommand(
                    std::make_unique<BlendModeChangeCommand>(
                        activeIdx,
                        static_cast<int>(layers[activeIdx]->blendMode),
                        currentBlendInt
                    )
                );
                layers[activeIdx]->blendMode = static_cast<LayerBlendMode>(currentBlendInt);
            }

            float& currentOpacity = layers[activeIdx]->opacity;

            // We use a static variable to remember what the opacity was BEFORE we started dragging
            static float initialOpacity = 1.0f;

            // 1. Grab a snapshot of the opacity BEFORE the slider has a chance to modify it
            float opacityBeforeEdit = currentOpacity;

            ImGui::SliderFloat("Layer Opacity", &currentOpacity, 0.0f, 1.0f, "%.2f");

            // 2. Triggered the exact frame the user CLICKS the slider
            if (ImGui::IsItemActivated()) {
                // Save the un-modified snapshot
                initialOpacity = opacityBeforeEdit;
            }

            // 3. Triggered the exact frame the user RELEASES the mouse button
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                app.getCanvas().pushUndoCommand(
                    std::make_unique<OpacityChangeCommand>(activeIdx, initialOpacity, currentOpacity)
                );
            }
        }

        ImGui::Spacing();

        ImGui::BeginChild("LayerList", ImVec2(0, 150), true);

        // --- ESCAPE HATCH (Drop here to pull out of all folders) ---
        ImGui::Selectable("--- Top of Layer Stack ---", false, ImGuiSelectableFlags_Disabled);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DRAG_AND_DROP")) {
                int draggedIndex = *(const int*)payload->Data;
                app.getCanvas().removeFromFolder(draggedIndex);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();

        // --- HIERARCHY SORTING ---
        // We calculate the correct visual order so Folders appear ABOVE their contents!
        std::vector<int> uiOrder;
        std::vector<bool> processed(layers.size(), false);

        std::function<void(int, int, int)> buildUiOrder = [&](int startIdx, int endIdx, int targetDepth) {
            for (int i = endIdx; i >= startIdx; --i) {
                if (!processed[i] && layers[i]->depth == targetDepth) {
                    processed[i] = true;
                    if (layers[i]->type == LayerType::Folder) {
                        uiOrder.push_back(i); // Print the Folder header first!

                        // Find the block of children belonging to this folder
                        int childStart = i + 1;
                        int childEnd = childStart;
                        while (childEnd <= endIdx && layers[childEnd]->depth > targetDepth) {
                            childEnd++;
                        }

                        // Recursively process the children so they appear immediately under the folder
                        if (childEnd > childStart) {
                            buildUiOrder(childStart, childEnd - 1, targetDepth + 1);
                        }
                    }
                    else {
                        uiOrder.push_back(i); // Print a regular layer
                    }
                }
            }
            };

        if (!layers.empty()) {
            buildUiOrder(0, layers.size() - 1, 0);
        }

        // Failsafe for any malformed depths during drag-and-drop
        for (int i = layers.size() - 1; i >= 0; --i) {
            if (!processed[i]) uiOrder.push_back(i);
        }

        // --- RENDER THE UI LIST ---
        for (int i : uiOrder) {
            ImGui::PushID(i);

            // Visual indentation width
            float indentSize = layers[i]->depth * 15.0f;
            ImGui::Indent(indentSize);

            ImGui::Checkbox("##vis", &layers[i]->visible);
            ImGui::SameLine();

            // Krita-style visual tree prefixes
            std::string prefix = "";
            if (layers[i]->type == LayerType::Folder) {
                prefix = "[F] ";
            }
            else if (layers[i]->depth > 0) {
                prefix = " \\_ ";
            }
            else {
                prefix = "    ";
            }
            std::string label = prefix + layers[i]->name;

            bool isSelected = (app.getCanvas().getActiveLayerIndex() == i);

            // Render the Selectable (spanning the remaining width for easy dropping)
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                app.getCanvas().setActiveLayer(i);
            }

            // --- 1. DRAG SOURCE ---
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("LAYER_DRAG_AND_DROP", &i, sizeof(int));
                ImGui::Text("Moving: %s", layers[i]->name.c_str());
                ImGui::EndDragDropSource();
            }

            // --- 2. DROP TARGET ---
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DRAG_AND_DROP")) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int draggedIndex = *(const int*)payload->Data;

                    if (draggedIndex != i) {
                        if (layers[i]->type == LayerType::Folder) {
                            app.getCanvas().moveToFolder(draggedIndex, i);
                        }
                        else {
                            app.getCanvas().dropLayerToReorder(draggedIndex, i);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Unindent(indentSize);
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
