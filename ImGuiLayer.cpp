#include "ImGuiLayer.h"
#include "Application.h"
#include "LayerUndoCommands.h"
#include "FileDialogs.h"
#include "ClipboardHelper.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <cstring>
#include <map>
#include <algorithm>

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

        static int canvasWidth = 1920;
        static int canvasHeight = 1080;

        ImGui::Text("Enter desired canvas dimensions:");
        ImGui::Spacing();

        ImGui::InputInt("Width", &canvasWidth);
        ImGui::InputInt("Height", &canvasHeight);

        if (canvasWidth < 100) canvasWidth = 100;
        if (canvasHeight < 100) canvasHeight = 100;

        ImGui::Spacing();

        float windowWidth = ImGui::GetWindowSize().x;
        float textWidth = ImGui::CalcTextSize("Create Canvas").x;
        ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);

        if (ImGui::Button("Create Canvas", ImVec2(150, 40))) {
            app.startDrawing(static_cast<unsigned int>(canvasWidth), static_cast<unsigned int>(canvasHeight));
        }

        ImGui::End();
    }
    else if (app.getState() == AppState::DrawingEditor) {

        // --- DEFERRED UI VARIABLES ---
        static int layerToDelete = -1;
        static std::vector<int> instantDeleteList;

        static int layerToRename = -1;
        static char renameBuffer[256] = "";

        static int layerToMergeDown = -1;
        static int folderToMerge = -1;
        // -----------------------------

        // --- BRUSH SETTINGS SIDEBAR ---
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.f, static_cast<float>(app.getWindowSize().y)), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoTitleBar;

        ImGui::Begin("Brush Settings", nullptr, flags);
        ImGui::PushItemWidth(-130.0f);

        // --- TOOL SELECTOR ---
        ImGui::TextDisabled("Tools");
        ImGui::Spacing();
        int currentTool = app.getToolMode();

        ImVec2 iconSize(36, 36); // Square buttons

        // Helper lambda to draw a professional, toggleable tool button
        auto DrawToolButton = [&](const char* iconText, const char* tooltip, int toolID) {
            bool isActive = (currentTool == toolID);

            if (isActive) {
                // Highlight the button with the "Active" color so it looks equipped
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            }
            else {
                // Make unequipped tools transparent
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            }

            if (ImGui::Button(iconText, iconSize)) {
                app.setToolMode(toolID);
            }

            // Add tooltips since we are using symbols instead of full words
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }

            ImGui::PopStyleColor();
            };

        // Draw the Tool Grid
        DrawToolButton("Brush", "Standard Brush", 0);
        ImGui::SameLine();
        DrawToolButton("[  ]", "Rectangle Select", 1);
        ImGui::SameLine();
        DrawToolButton(" ~ ", "Freehand Select", 2);

        ImGui::Separator();
        ImGui::Spacing();

        // --- FILE MENU ---
        ImGui::Text("File Management");
        ImGui::Spacing();

        if (ImGui::Button("Save As...", ImVec2(130, 0))) {
            std::string filepath = FileDialogs::saveFile("PNG Image (*.png)\0*.png\0Any File\0*.*\0");
            if (!filepath.empty()) app.getCanvas().saveToFile(filepath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Import...", ImVec2(130, 0))) {
            std::string filepath = FileDialogs::openFile("PNG Image (*.png)\0*.png\0Any File\0*.*\0");
            if (!filepath.empty()) app.getCanvas().loadFromFile(filepath);
        }

        // --- PASTE FROM CLIPBOARD ---
        if (ImGui::Button("Paste from Clipboard", ImVec2(-1, 0))) {
            sf::Image clipboardImg = ClipboardHelper::getImage();
            if (clipboardImg.getSize().x > 0) {
                app.getCanvas().importFromImage(clipboardImg, "Pasted Layer");
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        // --- BRUSH SETTINGS ---
        // Automatically gray out these sliders if the Rectangle Selection tool is selected
        ImGui::BeginDisabled(currentTool == 1);

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
        if (ImGui::Checkbox("Eraser Mode", &isEraser)) app.setEraser(isEraser);
        ImGui::Separator();

        // Brush Sliders
        float size = app.getBrushSize();
        if (ImGui::SliderFloat("Brush Size", &size, 1.f, 50.f)) app.setBrushSize(size);

        float smoothing = app.getBrushSmoothing();
        if (ImGui::SliderFloat("Path Smoothing", &smoothing, 0.f, 0.999f)) app.setBrushSmoothing(smoothing);

        float flow = app.getBrushFlow();
        if (ImGui::SliderFloat("Paint Flow", &flow, 0.1f, 1.0f)) app.setBrushFlow(flow);

        float softness = app.getBrushSoftness();
        if (ImGui::SliderFloat("Brush Softness", &softness, 1.0f, 15.0f)) app.setBrushSoftness(softness);

        ImGui::EndDisabled();
        ImGui::PopItemWidth();

        // --- LAYERS PANEL ---
        ImGui::Separator();
        ImGui::Text("Layers");
        ImGui::Spacing();

        if (ImGui::Button("Add Layer", ImVec2(130, 0))) app.getCanvas().addLayer();
        ImGui::SameLine();
        if (ImGui::Button("Add Folder", ImVec2(130, 0))) app.getCanvas().addFolder();
        ImGui::Spacing();

        auto& layers = app.getCanvas().getLayers();
        int activeIdx = app.getCanvas().getActiveLayerIndex();
        const auto& selectedLayers = app.getCanvas().getSelectedLayers();

        if (activeIdx >= 0 && activeIdx < layers.size()) {

            // BATCH BLEND MODE
            if (layers[activeIdx]->isClipped) {
                ImGui::BeginDisabled();
                int dummy = 0;
                ImGui::Combo("Blend Mode", &dummy, "Normal (Clipped)\0");
                ImGui::EndDisabled();
            }
            else {
                const char* blendModeNames[] = { "Normal", "Multiply", "Add (Linear Dodge)", "Pass Through" };
                int currentBlendInt = static_cast<int>(layers[activeIdx]->blendMode);

                if (ImGui::Combo("Blend Mode", &currentBlendInt, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {
                    app.getCanvas().beginBatchCommand();
                    for (int sel : selectedLayers) {
                        int finalBlend = currentBlendInt;
                        if (layers[sel]->type == LayerType::Content && finalBlend == 3) finalBlend = 0;
                        app.getCanvas().pushUndoCommand(std::make_unique<BlendModeChangeCommand>(sel, static_cast<int>(layers[sel]->blendMode), finalBlend));
                        layers[sel]->blendMode = static_cast<LayerBlendMode>(finalBlend);
                    }
                    app.getCanvas().endBatchCommand();
                }
            }

            // BATCH OPACITY
            float& currentOpacity = layers[activeIdx]->opacity;
            static std::map<int, float> initialOpacities;

            if (ImGui::SliderFloat("Layer Opacity", &currentOpacity, 0.0f, 1.0f, "%.2f")) {
                for (int sel : selectedLayers) {
                    layers[sel]->opacity = currentOpacity;
                }
            }
            if (ImGui::IsItemActivated()) {
                initialOpacities.clear();
                for (int sel : selectedLayers) initialOpacities[sel] = layers[sel]->opacity;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                app.getCanvas().beginBatchCommand();
                for (int sel : selectedLayers) {
                    if (initialOpacities.count(sel)) {
                        app.getCanvas().pushUndoCommand(std::make_unique<OpacityChangeCommand>(sel, initialOpacities[sel], layers[sel]->opacity));
                    }
                }
                app.getCanvas().endBatchCommand();
            }
        }

        ImGui::Spacing();
        ImGui::BeginChild("LayerList", ImVec2(0, 150), true);

        ImGui::Selectable("--- Top of Layer Stack ---", false, ImGuiSelectableFlags_Disabled);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DRAG_AND_DROP")) {
                int draggedIndex = *(const int*)payload->Data;
                app.getCanvas().removeFromFolder(draggedIndex);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();

        std::vector<int> uiOrder;
        std::vector<bool> processed(layers.size(), false);

        std::function<void(int, int, int)> buildUiOrder = [&](int startIdx, int endIdx, int targetDepth) {
            for (int i = endIdx; i >= startIdx; --i) {
                if (!processed[i] && layers[i]->depth == targetDepth) {
                    processed[i] = true;
                    if (layers[i]->type == LayerType::Folder) {
                        uiOrder.push_back(i);
                        int childStart = i + 1;
                        int childEnd = childStart;
                        while (childEnd <= endIdx && layers[childEnd]->depth > targetDepth) childEnd++;
                        if (childEnd > childStart) buildUiOrder(childStart, childEnd - 1, targetDepth + 1);
                    }
                    else {
                        uiOrder.push_back(i);
                    }
                }
            }
            };

        if (!layers.empty()) buildUiOrder(0, layers.size() - 1, 0);
        for (int i = layers.size() - 1; i >= 0; --i) {
            if (!processed[i]) uiOrder.push_back(i);
        }

        // --- RENDER THE UI LIST ---
        for (int i : uiOrder) {
            ImGui::PushID(i);

            float indentSize = layers[i]->depth * 15.0f;
            ImGui::Indent(indentSize);

            ImGui::Checkbox("##vis", &layers[i]->visible);
            ImGui::SameLine();

            std::string prefix = "";
            if (layers[i]->type == LayerType::Folder) prefix = "[F] ";
            else if (layers[i]->depth > 0) prefix = " \\_ ";
            else prefix = "    ";
            if (layers[i]->isClipped) prefix += "-> ";

            std::string label = prefix + layers[i]->name;

            // MULTI-SELECTABLE
            bool isSelected = app.getCanvas().isLayerSelected(i);
            float availableWidth = ImGui::GetContentRegionAvail().x;

            if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(availableWidth - 30.0f, 0))) {
                app.getCanvas().toggleLayerSelection(i, ImGui::GetIO().KeyCtrl);
            }

            // --- RIGHT-CLICK CONTEXT MENU ---
            if (ImGui::BeginPopupContextItem()) {
                if (!app.getCanvas().isLayerSelected(i)) {
                    app.getCanvas().toggleLayerSelection(i, false);
                }

                const auto& activeSelection = app.getCanvas().getSelectedLayers();

                ImGui::TextDisabled("Layer Actions");
                ImGui::Separator();

                if (layers[i]->type == LayerType::Content) {

                    bool allAlphaLocked = layers[i]->alphaLocked;
                    if (ImGui::Checkbox("Alpha Lock", &allAlphaLocked)) {
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content) layers[sel]->alphaLocked = allAlphaLocked;
                        }
                    }

                    ImGui::BeginDisabled(i == 0);
                    bool isClipped = layers[i]->isClipped;
                    if (ImGui::Checkbox("Clipping Mask", &isClipped)) {
                        app.getCanvas().beginBatchCommand();
                        for (int sel : activeSelection) {
                            if (sel > 0 && layers[sel]->type == LayerType::Content) {
                                app.getCanvas().pushUndoCommand(std::make_unique<ClipLayerCommand>(sel, layers[sel]->isClipped, isClipped));
                                layers[sel]->isClipped = isClipped;
                            }
                        }
                        app.getCanvas().endBatchCommand();
                    }
                    ImGui::EndDisabled();
                    ImGui::Separator();

                    static std::map<int, std::unique_ptr<sf::Image>> transformBackups;

                    // MULTI-POSITION SLIDERS
                    float pos[2] = { layers[i]->offset.x, layers[i]->offset.y };
                    if (ImGui::DragFloat2("Move (X,Y)", pos, 1.0f)) {
                        float dx = pos[0] - layers[i]->offset.x;
                        float dy = pos[1] - layers[i]->offset.y;
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content) {
                                layers[sel]->offset.x += dx;
                                layers[sel]->offset.y += dy;
                            }
                        }
                    }
                    if (ImGui::IsItemActivated()) {
                        transformBackups.clear();
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content) {
                                transformBackups[sel] = std::make_unique<sf::Image>(layers[sel]->texture->getTexture().copyToImage());
                            }
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        app.getCanvas().beginBatchCommand();
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content && transformBackups.count(sel)) {
                                app.getCanvas().bakeLayerTransform(sel, std::move(transformBackups[sel]));
                            }
                        }
                        app.getCanvas().endBatchCommand();
                    }

                    // MULTI-SCALE SLIDERS
                    float scl[2] = { layers[i]->scale.x, layers[i]->scale.y };
                    if (ImGui::DragFloat2("Scale (X,Y)", scl, 0.01f)) {
                        float dx = scl[0] - layers[i]->scale.x;
                        float dy = scl[1] - layers[i]->scale.y;
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content) {
                                layers[sel]->scale.x += dx;
                                layers[sel]->scale.y += dy;
                            }
                        }
                    }
                    if (ImGui::IsItemActivated()) {
                        transformBackups.clear();
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content) {
                                transformBackups[sel] = std::make_unique<sf::Image>(layers[sel]->texture->getTexture().copyToImage());
                            }
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        app.getCanvas().beginBatchCommand();
                        for (int sel : activeSelection) {
                            if (layers[sel]->type == LayerType::Content && transformBackups.count(sel)) {
                                app.getCanvas().bakeLayerTransform(sel, std::move(transformBackups[sel]));
                            }
                        }
                        app.getCanvas().endBatchCommand();
                    }

                    // MERGE DOWN
                    if (i > 0 && layers[i - 1]->type == LayerType::Content) {
                        if (ImGui::Selectable("Merge Down")) layerToMergeDown = i;
                    }
                }

                // MERGE FOLDER
                if (layers[i]->type == LayerType::Folder) {
                    if (ImGui::Selectable("Merge Folder")) folderToMerge = i;
                }

                ImGui::Separator();

                // RENAME
                if (ImGui::Selectable("Rename Layer/Folder")) {
                    layerToRename = i;
                    #ifdef _MSC_VER
                    strncpy_s(renameBuffer, layers[i]->name.c_str(), sizeof(renameBuffer) - 1);
                    #else
                    strncpy(renameBuffer, layers[i]->name.c_str(), sizeof(renameBuffer) - 1);
                    #endif
                    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                }

                // MULTI-DELETE
                ImGui::Separator();
                if (ImGui::Selectable("Delete Selected")) {
                    std::vector<int> sortedSels(activeSelection.begin(), activeSelection.end());
                    std::sort(sortedSels.rbegin(), sortedSels.rend());

                    bool hasChildren = false;
                    for (int sel : sortedSels) {
                        if (layers[sel]->type == LayerType::Folder && sel + 1 < layers.size() && layers[sel + 1]->depth > layers[sel]->depth) {
                            hasChildren = true;
                        }
                    }

                    if (hasChildren) {
                        layerToDelete = i;
                    }
                    else {
                        instantDeleteList = sortedSels;
                    }
                }

                ImGui::EndPopup();
            }

            // ALPHA LOCK SYMBOL
            if (layers[i]->alphaLocked) {
                ImGui::SameLine();
                ImGui::TextDisabled("[A]");
            }

            // DRAG SOURCE
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("LAYER_DRAG_AND_DROP", &i, sizeof(int));
                ImGui::Text("Moving: %s", layers[i]->name.c_str());
                ImGui::EndDragDropSource();
            }

            // DROP TARGET
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DRAG_AND_DROP")) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int draggedIndex = *(const int*)payload->Data;

                    if (draggedIndex != i) {
                        if (layers[i]->type == LayerType::Folder) app.getCanvas().moveToFolder(draggedIndex, i);
                        else app.getCanvas().dropLayerToReorder(draggedIndex, i);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Unindent(indentSize);
            ImGui::PopID();
        }
        ImGui::EndChild();

        // --- SAFELY PROCESS DEFERRED ACTIONS ---
        if (!instantDeleteList.empty()) {
            app.getCanvas().beginBatchCommand();
            for (int idx : instantDeleteList) app.getCanvas().deleteLayer(idx);
            app.getCanvas().endBatchCommand();
            instantDeleteList.clear();
        }
        if (layerToMergeDown != -1) {
            app.getCanvas().mergeDown(layerToMergeDown);
            layerToMergeDown = -1;
        }
        if (folderToMerge != -1) {
            app.getCanvas().mergeFolder(folderToMerge);
            folderToMerge = -1;
        }
        // ---------------------------------------

        // --- MODALS ---
        if (layerToRename != -1 && !ImGui::IsPopupOpen("Rename Layer")) ImGui::OpenPopup("Rename Layer");
        if (layerToDelete != -1) ImGui::OpenPopup("Delete Folder Warning");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();

        // Rename Modal
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Rename Layer", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter new name:");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

            bool enterPressed = ImGui::InputText("##newName", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();

            if (ImGui::Button("Rename", ImVec2(120, 0)) || enterPressed) {
                std::string newName(renameBuffer);
                if (!newName.empty() && newName != layers[layerToRename]->name) {
                    app.getCanvas().pushUndoCommand(std::make_unique<RenameLayerCommand>(layerToRename, layers[layerToRename]->name, newName));
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
        else if (layerToRename != -1 && !ImGui::IsPopupOpen("Rename Layer")) layerToRename = -1;

        // Delete Modal (Updated to support Batch Delete)
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete Folder Warning", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("This folder contains other layers.");
            ImGui::Text("Deleting it will permanently remove EVERYTHING inside it!");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Delete All", ImVec2(120, 0))) {
                const auto& activeSelection = app.getCanvas().getSelectedLayers();
                std::vector<int> sortedSels(activeSelection.begin(), activeSelection.end());
                std::sort(sortedSels.rbegin(), sortedSels.rend());

                app.getCanvas().beginBatchCommand();
                for (int sel : sortedSels) app.getCanvas().deleteLayer(sel);
                app.getCanvas().endBatchCommand();

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
        else if (layerToDelete != -1 && !ImGui::IsPopupOpen("Delete Folder Warning")) layerToDelete = -1;

        // --- RIGHT-CLICK CANVAS CONTEXT MENU ---
        if (app.getCanvas().hasSelection()) {
            // "BeginPopupContextVoid" opens a popup at your mouse cursor anytime the user right-clicks the background workspace
            if (ImGui::BeginPopupContextVoid("CanvasContextMenu")) {
                ImGui::TextDisabled("Selection Options");
                ImGui::Separator();

                if (ImGui::Selectable("Delete Selected Area")) {
                    app.getCanvas().clearSelectionOnSelectedLayers();
                }
                if (ImGui::Selectable("Deselect")) {
                    app.getCanvas().getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
                    app.getCanvas().setSelectionActive(false);
                }
                ImGui::EndPopup();
            }
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