#include "ImGuiLayer.h"
#include "Application.h"
#include "LayerUndoCommands.h"
#include "FileDialogs.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <cstring>

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
        static int layerToDelete = -1;
        static int layerToDeleteInstantly = -1;

        static int layerToRename = -1;
        static char renameBuffer[256] = "";

        // --- BRUSH SETTINGS SIDEBAR ---
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.f, static_cast<float>(app.getWindowSize().y)), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoTitleBar;

        ImGui::Begin("Brush Settings", nullptr, flags);
        ImGui::PushItemWidth(-130.0f);

        // --- FILE MENU ---
        ImGui::Text("File Management");
        ImGui::Spacing();

        if (ImGui::Button("Save As...", ImVec2(130, 0))) {
            // The filter format is: "Display Name\0*.extension\0"
            std::string filepath = FileDialogs::saveFile("PNG Image (*.png)\0*.png\0Any File\0*.*\0");

            if (!filepath.empty()) {
                app.getCanvas().saveToFile(filepath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Import...", ImVec2(130, 0))) {
            std::string filepath = FileDialogs::openFile("PNG Image (*.png)\0*.png\0Any File\0*.*\0");

            if (!filepath.empty()) {
                app.getCanvas().loadFromFile(filepath);
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

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
            if (layers[activeIdx]->isClipped) {
                // Disable the dropdown to indicate clipping forces standard masking logic
                ImGui::BeginDisabled();
                int dummy = 0;
                ImGui::Combo("Blend Mode", &dummy, "Normal (Clipped)\0");
                ImGui::EndDisabled();
            }
            else {
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
        // We calculate the correct visual order so Folders appear above their contents
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

            // Visual tree prefixes
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
            if (layers[i]->isClipped) {
                prefix += "-> ";
            }

            std::string label = prefix + layers[i]->name;

            bool isSelected = (app.getCanvas().getActiveLayerIndex() == i);

            // Calculate exact remaining space to avoid ImGui's negative-width clipping bug!
            float availableWidth = ImGui::GetContentRegionAvail().x;

            // Leave exactly 30 pixels of space on the right side
            if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(availableWidth - 30.0f, 0))) {
                app.getCanvas().setActiveLayer(i);
            }

            // --- RIGHT-CLICK CONTEXT MENU ---
            if (ImGui::BeginPopupContextItem()) {
                app.getCanvas().setActiveLayer(i);

                ImGui::TextDisabled("Layer Actions");
                ImGui::Separator();

                // 1. CONTENT-ONLY ACTIONS (Alpha Lock, Move, Scale)
                // These should ONLY appear for actual drawing layers
                if (layers[i]->type == LayerType::Content) {

                    ImGui::Checkbox("Alpha Lock", &layers[i]->alphaLocked);
                    // --- CLIPPING MASK TOGGLE ---
                    // Disable clipping for the absolute bottom layer since there's nothing below it to clip to
                    ImGui::BeginDisabled(i == 0);
                    bool isClipped = layers[i]->isClipped;
                    if (ImGui::Checkbox("Clipping Mask", &isClipped)) {
                        app.getCanvas().pushUndoCommand(std::make_unique<ClipLayerCommand>(i, layers[i]->isClipped, isClipped));
                        layers[i]->isClipped = isClipped;
                    }
                    ImGui::EndDisabled();
                    ImGui::Separator();

                    static std::unique_ptr<sf::Image> transformBackup;

                    // POSITION SLIDERS
                    float pos[2] = { layers[i]->offset.x, layers[i]->offset.y };
                    if (ImGui::DragFloat2("Move (X,Y)", pos, 1.0f)) {
                        layers[i]->offset = { pos[0], pos[1] };
                    }
                    if (ImGui::IsItemActivated()) {
                        transformBackup = std::make_unique<sf::Image>(layers[i]->texture->getTexture().copyToImage());
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        app.getCanvas().bakeLayerTransform(i, std::move(transformBackup));
                    }

                    // SCALE SLIDERS
                    float scl[2] = { layers[i]->scale.x, layers[i]->scale.y };
                    if (ImGui::DragFloat2("Scale (X,Y)", scl, 0.01f)) {
                        layers[i]->scale = { scl[0], scl[1] };
                    }
                    if (ImGui::IsItemActivated()) {
                        transformBackup = std::make_unique<sf::Image>(layers[i]->texture->getTexture().copyToImage());
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        app.getCanvas().bakeLayerTransform(i, std::move(transformBackup));
                    }

                } // <--- IMPORTANT: The Content-Only block MUST close here!

                // 2. GLOBAL ACTIONS (Rename, Delete)
                // These will now safely appear for BOTH Layers and Folders!

                ImGui::Separator();

                // --- RENAME BUTTON ---
                if (ImGui::Selectable("Rename Layer/Folder")) {
                    layerToRename = i;
                    // Pre-fill the text box with the current name
                    #ifdef _MSC_VER
                    strncpy_s(renameBuffer, layers[i]->name.c_str(), sizeof(renameBuffer) - 1);
                    #else
                    strncpy(renameBuffer, layers[i]->name.c_str(), sizeof(renameBuffer) - 1);
                    #endif
                    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                }

                // --- DELETE BUTTON ---
                ImGui::Separator();
                if (ImGui::Selectable("Delete Layer/Folder")) {
                    bool hasChildren = false;

                    // Check if it's a folder that actually has items inside it
                    if (layers[i]->type == LayerType::Folder) {
                        if (i + 1 < layers.size() && layers[i + 1]->depth > layers[i]->depth) {
                            hasChildren = true;
                        }
                    }

                    if (hasChildren) {
                        layerToDelete = i; // Trigger the warning modal
                    }
                    else {
                        layerToDeleteInstantly = i; // Defer safe deletion
                    }
                }

                ImGui::EndPopup();
            }


            // --- ALPHA LOCK SYMBOL ---
            if (layers[i]->alphaLocked) {
                ImGui::SameLine();
                ImGui::TextDisabled("[A]");
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

        // --- SAFELY PROCESS DEFERRED DELETION ---
        if (layerToDeleteInstantly != -1) {
            app.getCanvas().deleteLayer(layerToDeleteInstantly);
            layerToDeleteInstantly = -1;
        }

        // --- DELETE WARNING MODAL ---
        if (layerToDelete != -1) {
            ImGui::OpenPopup("Delete Folder Warning");
        }

        // --- RENAME MODAL ---
        if (layerToRename != -1 && !ImGui::IsPopupOpen("Rename Layer")) {
            ImGui::OpenPopup("Rename Layer");
        }

        // Always center the modal on the screen
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Rename Layer", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter new name:");

            // This trick auto-selects the text box so you can start typing immediately!
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

            // ImGuiInputTextFlags_EnterReturnsTrue allows hitting the "Enter" key to submit
            bool enterPressed = ImGui::InputText("##newName", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Spacing();

            if (ImGui::Button("Rename", ImVec2(120, 0)) || enterPressed) {
                std::string newName(renameBuffer);
                // Only save the command if the name actually changed and isn't completely empty
                if (!newName.empty() && newName != layers[layerToRename]->name) {
                    app.getCanvas().pushUndoCommand(std::make_unique<RenameLayerCommand>(
                        layerToRename,
                        layers[layerToRename]->name,
                        newName
                    ));
                    layers[layerToRename]->name = newName;
                }
                layerToRename = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                layerToRename = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else if (layerToRename != -1 && !ImGui::IsPopupOpen("Rename Layer")) {
            layerToRename = -1; // Failsafe
        }

        if (ImGui::BeginPopupModal("Delete Folder Warning", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("This folder contains other layers.");
            ImGui::Text("Deleting it will permanently remove EVERYTHING inside it!");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Delete All", ImVec2(120, 0))) {
                app.getCanvas().deleteLayer(layerToDelete);
                layerToDelete = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                layerToDelete = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else if (layerToDelete != -1 && !ImGui::IsPopupOpen("Delete Folder Warning")) {
            // Failsafe in case user presses ESC to force-close the modal
            layerToDelete = -1;
        }

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
