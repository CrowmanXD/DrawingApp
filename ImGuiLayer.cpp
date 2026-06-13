#include "ImGuiLayer.h"
#include "Application.h"
#include "LayerUndoCommands.h"
#include "FileDialogs.h"
#include "ClipboardHelper.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include <fstream>
#include <cstdint>
#include <functional>
#include <vector>
#include <cstring>
#include <map>
#include <algorithm>
#include <cfloat>

ImGuiLayer::ImGuiLayer() = default;
ImGuiLayer::~ImGuiLayer() = default;

void ImGuiLayer::init(sf::RenderWindow& window) {
    // Pass 'false' to stop ImGui from loading the pixel font into Slot 0
    ImGui::SFML::Init(window, false);

    ImGuiIO& io = ImGui::GetIO();
    std::string fontPath = "fonts/Roboto-VariableFont_wdth,wght.ttf";

    // Check if the file exists using native C++
    std::ifstream fontFile(fontPath);
    if (fontFile.good()) {
        fontFile.close();
        // Loads into Slot 0, becoming the global default!
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f);
    }
    else {
        printf("[GUI WARNING] Failed to find font at: %s. Falling back to default pixel font.\n", fontPath.c_str());
        // Safe fallback since the automatic initialization is disabled
        io.Fonts->AddFontDefault();
    }

    // Bake whichever font is loaded into the GPU texture
    ImGui::SFML::UpdateFontTexture();

    loadIcons();
}

void ImGuiLayer::loadIcons() {
    // Helper lambda to load textures and enable smooth scaling (Anti-aliasing)
    auto load = [&](const std::string& name, const std::string& path) {
        sf::Texture tex;
        if (tex.loadFromFile(path)) {
            tex.setSmooth(true);
            m_icons[name] = std::move(tex);
        }
        };

    // Load your main tools
    load("brush", "icons/Icon_brush.png");
    load("rect", "icons/Icon_BoxSelect.png");
    load("lasso", "icons/Icon_FreeSelect.png");
    load("pen", "icons/Icon_VectorPen.png");
    load("bucket", "icons/Icon_PaintBucket.png");
    load("shapes", "icons/Icon_ShapeTool.png");
    load("selectbrush", "icons/Icon_SelectionBrush.png");
    load("crop", "icons/Icon_Crop.png");
    load("transform", "icons/Icon_Transform.png");
    load("picker", "icons/Icon_ColorPicker.png");
    load("lock", "icons/Icon_Lock.png");

    // Load your shape sub-menu icons
    load("shape_square", "icons/Icon_ShapeTool.png");
    load("shape_circle", "icons/Icon_ShapeTool_circle.png");
    load("shape_diamond", "icons/Icon_ShapeTool_diamond.png");
    load("shape_triangle", "icons/Icon_ShapeTool_triangle.png");
    load("shape_star", "icons/Icon_ShapeTool_star.png");
    load("shape_heart", "icons/Icon_ShapeTool_heart.png");
}

void ImGuiLayer::processEvent(sf::RenderWindow& window, const sf::Event& event) {
    ImGui::SFML::ProcessEvent(window, event);
}

void ImGuiLayer::update(sf::RenderWindow& window, sf::Time deltaTime, Application& app) {
    ImGui::SFML::Update(window, deltaTime);
    static int lastTheme = -1;
    if (app.getTheme() != lastTheme) {
        lastTheme = app.getTheme();
        if (lastTheme == 0) ImGui::StyleColorsDark();
        else if (lastTheme == 1) ImGui::StyleColorsLight();
        else if (lastTheme == 2) ImGui::StyleColorsClassic();
    }

    static bool openSettingsModal = false;
    if (app.getState() == AppState::StartupScreen) {
        ImVec2 center(window.getSize().x * 0.5f, window.getSize().y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_Always);

        // Make the window clean and borderless
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;

        ImGui::Begin("Welcome Screen", nullptr, flags);
        float buttonWidth = 220.f;
        // --- TITLE ---
        ImGui::Spacing(); ImGui::Spacing();
        const char* title = "Welcome to Licenta Desen C++";
        float textWidth = ImGui::CalcTextSize(title).x;

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", title); // Blue tint
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();

        // --- MAIN BUTTONS ---
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);
        if (ImGui::Button("New Project", ImVec2(buttonWidth, 45))) {
            ImGui::OpenPopup("New Project Settings");
        }

        ImGui::Spacing(); ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);
        if (ImGui::Button("Open Project", ImVec2(buttonWidth, 45))) {
            std::string filepath = FileDialogs::openFile("Drawing Project (*.drw)\0*.drw\0Any File\0*.*\0");
            if (!filepath.empty()) {
                app.loadProject(filepath);
            }
        }

        ImGui::Spacing(); ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonWidth) * 0.5f);
        if (ImGui::Button("Settings", ImVec2(buttonWidth, 45))) {
            openSettingsModal = true;
        }

        // ==========================================
        //  MODALS
        // ==========================================

        // --- NEW PROJECT MODAL ---
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("New Project Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static int canvasWidth = 1920;
            static int canvasHeight = 1080;

            ImGui::Text("Enter desired canvas dimensions:");
            ImGui::Spacing();

            ImGui::InputInt("Width", &canvasWidth);
            ImGui::InputInt("Height", &canvasHeight);

            if (canvasWidth < 100) canvasWidth = 100;
            if (canvasHeight < 100) canvasHeight = 100;

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                app.startDrawing(static_cast<unsigned int>(canvasWidth), static_cast<unsigned int>(canvasHeight));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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

        float windowHeight = static_cast<float>(app.getWindowSize().y);
        float windowWidth = static_cast<float>(app.getWindowSize().x);
        float menuBarHeight = 0.f;

        // ==========================================
        //  TOP MAIN MENU BAR
        // ==========================================
        if (ImGui::BeginMainMenuBar()) {
            menuBarHeight = ImGui::GetWindowSize().y;

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open Project...")) {
                    std::string filepath = FileDialogs::openFile("Drawing Project (*.drw)\0*.drw\0Any File\0*.*\0");
                    if (!filepath.empty()) app.loadProject(filepath); // Routes through App to remember path
                }
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                    app.saveCurrentProject();
                }
                if (ImGui::MenuItem("Save Project As...")) {
                    app.saveProjectAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Image...")) {
                    std::string filepath = FileDialogs::openFile("PNG Image (*.png)\0*.png\0Any File\0*.*\0");
                    if (!filepath.empty()) app.getCanvas().loadFromFile(filepath);
                }
                if (ImGui::MenuItem("Export Image...")) {
                    std::string filepath = FileDialogs::saveFile("PNG Image (*.png)\0*.png\0Any File\0*.*\0");
                    if (!filepath.empty()) app.getCanvas().saveToFile(filepath);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Paste from Clipboard")) {
                    sf::Image clipboardImg = ClipboardHelper::getImage();
                    if (clipboardImg.getSize().x > 0) app.getCanvas().importFromImage(clipboardImg, "Pasted Layer");
                }
                ImGui::EndMenu();
            }

            // Visual Placeholders for future expansion
            if (ImGui::BeginMenu("View")) {

                if (ImGui::MenuItem("Mirror Canvas", "M")) {
                    app.getCanvas().flipCanvasHorizontal();
                }

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Image")) {ImGui::EndMenu();}
            if (ImGui::BeginMenu("Layer")) { ImGui::EndMenu(); }
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::MenuItem("App Preferences...")) {
                    openSettingsModal = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Filter")) { ImGui::EndMenu(); }

            ImGui::EndMainMenuBar();
        }

        ImGuiWindowFlags dockFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

        // ==========================================
        //  LEFT PANEL: THE TOOLBAR
        // ==========================================
        ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(70.f, windowHeight - menuBarHeight), ImGuiCond_Always);
        ImGui::Begin("Toolbar", nullptr, dockFlags);

        auto DrawToolButton = [&](const char* iconKey, const char* fallbackText, const char* tooltip, int toolId, const char* actionName) {
            bool isSelected = (app.getToolMode() == toolId);
            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));

            // Math to center the 35px buttons inside the 70px column
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 35.f) * 0.5f);

            if (m_icons.count(iconKey)) {
                if (ImGui::ImageButton(fallbackText, m_icons[iconKey], sf::Vector2f(35.f, 35.f))) app.setToolMode(toolId);
            }
            else {
                if (ImGui::Button(fallbackText, ImVec2(35, 35))) app.setToolMode(toolId);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", tooltip); // Normal white text

                std::string shortcut = app.getShortcutString(actionName);
                if (!shortcut.empty() && shortcut != "Unbound") {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", shortcut.c_str()); // Low-opacity text
                }
                ImGui::EndTooltip();
            }

            if (isSelected) ImGui::PopStyleColor();
            ImGui::Spacing(); // Adds a tiny gap between vertical tools
            };

        ImGui::Spacing(); ImGui::Spacing(); // Top padding

        // Pass the dictionary keys to the helper
        DrawToolButton("brush", "Br", "Standard Brush", 0, "Tool: Brush");
        DrawToolButton("rect", "[]", "Rectangle Select", 1, "Tool: Rect Select");
        DrawToolButton("selectbrush", " O ", "Selection Brush", 2, "Tool: Sel. Brush");
        DrawToolButton("pen", "~/", "Bezier Curve Pen", 3, "Tool: Pen");
        DrawToolButton("lasso", " ~ ", "Freehand Lasso", 4, "Tool: Lasso");
        DrawToolButton("bucket", "Bk", "Paint Bucket", 5, "Tool: Paint Bucket");
        DrawToolButton("shapes", "Sh", "Shapes", 6, "Tool: Shapes");
        DrawToolButton("crop", "[|]", "Crop Canvas", 7, "Tool: Crop");
        DrawToolButton("transform", "[T]", "Free Transform", 8, "Tool: Transform");
        DrawToolButton("picker", "Ey", "Color Picker", 9, "Tool: Color Picker");

        ImGui::End(); // End Left Toolbar

        // ==========================================
        //  RIGHT PANEL: PROPERTIES & LAYERS
        // ==========================================
        ImGui::SetNextWindowPos(ImVec2(windowWidth - 300.f, menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.f, windowHeight - menuBarHeight), ImGuiCond_Always);
        ImGui::Begin("Properties", nullptr, dockFlags);
        ImGui::PushItemWidth(-100.0f);

        // --- CONTEXTUAL SHAPE MENU (Only shows if Shape Tool is active) ---
        if (app.getToolMode() == 6) {
            ImGui::TextDisabled("Shape Settings");
            ImGui::Separator();

            const char* shapeKeys[] = { "shape_square", "shape_circle", "shape_diamond", "shape_triangle", "shape_star", "shape_heart" };
            const char* shapeIcons[] = { "[ ]", "( )", "< >", "/_\\", " * ", "<3" };
            const char* shapeNames[] = { "Square", "Circle", "Diamond", "Triangle", "Star", "Heart" };

            for (int i = 0; i < 6; ++i) {
                if (i > 0) ImGui::SameLine();
                bool isSelected = (app.getShapeType() == i);
                if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));

                if (m_icons.count(shapeKeys[i])) {
                    if (ImGui::ImageButton(shapeNames[i], m_icons[shapeKeys[i]], sf::Vector2f(35.f, 35.f))) app.setShapeType(i);
                }
                else {
                    if (ImGui::Button(shapeIcons[i], ImVec2(35, 35))) app.setShapeType(i);
                }

                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shapeNames[i]);
                if (isSelected) ImGui::PopStyleColor();
            }

            bool isFilled = app.isShapeFilled();
            if (ImGui::Checkbox("Fill", &isFilled)) app.setShapeFilled(isFilled);
            ImGui::Spacing(); ImGui::Spacing();
        }

        // --- CANVAS NAVIGATION ---
        ImGui::TextDisabled("Canvas Navigation");
        ImGui::Separator();

        float rot = app.getWorkspaceRotation();

        // Center the 3 quick-snap buttons
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 180.f) * 0.5f);
        if (ImGui::Button("< -15°", ImVec2(55, 0))) rot -= 15.f;
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(55, 0))) rot = 0.f;
        ImGui::SameLine();
        if (ImGui::Button("+15° >", ImVec2(55, 0))) rot += 15.f;

        // Wrap the angle so it stays between -180 and 180
        if (rot <= -180.f) rot += 360.f;
        if (rot > 180.f) rot -= 360.f;

        ImGui::Spacing();
        if (ImGui::SliderFloat("##WorkspaceRot", &rot, -180.f, 180.f, "Rotation: %.1f°")) {
            // Automatically updates 'rot' while sliding
        }

        app.setWorkspaceRotation(rot); // Send back to the engine
        ImGui::Spacing(); ImGui::Spacing();

        // --- BRUSH SETTINGS ---
        ImGui::TextDisabled("Tool Properties");
        ImGui::Separator();

        sf::Color currentColor = app.getBrushColor();

        // STATIC MEMORY:
        // It prevents the float->int->float conversion from destroying ImGui's precise HSV math.
        static float col[3] = { currentColor.r / 255.f, currentColor.g / 255.f, currentColor.b / 255.f };
        static float opacity = currentColor.a / 255.f;

        // Sync if the color was changed externally (e.g., by an Eyedropper tool or opening a file)
        sf::Color guiColor(
            static_cast<std::uint8_t>(col[0] * 255.f),
            static_cast<std::uint8_t>(col[1] * 255.f),
            static_cast<std::uint8_t>(col[2] * 255.f),
            static_cast<std::uint8_t>(opacity * 255.f)
        );

        if (currentColor != guiColor) {
            col[0] = currentColor.r / 255.f;
            col[1] = currentColor.g / 255.f;
            col[2] = currentColor.b / 255.f;
            opacity = currentColor.a / 255.f;
        }

        // COLOR WHEEL
        ImGuiColorEditFlags pickerFlags =
            ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_DisplayHSV |
            ImGuiColorEditFlags_DisplayHex |
            ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_NoAlpha; // Disables the inner alpha bar

        if (ImGui::ColorPicker3("##BrushColor", col, pickerFlags)) {
            app.setBrushColor(sf::Color(
                static_cast<std::uint8_t>(col[0] * 255.f),
                static_cast<std::uint8_t>(col[1] * 255.f),
                static_cast<std::uint8_t>(col[2] * 255.f),
                static_cast<std::uint8_t>(opacity * 255.f)
            ));
        }

        ImGui::Spacing();

        // SEPARATE OPACITY SLIDER
        if (ImGui::SliderFloat("##BrushOpacity", &opacity, 0.0f, 1.0f, "Opacity: %.2f")) {
            app.setBrushColor(sf::Color(
                static_cast<std::uint8_t>(col[0] * 255.f),
                static_cast<std::uint8_t>(col[1] * 255.f),
                static_cast<std::uint8_t>(col[2] * 255.f),
                static_cast<std::uint8_t>(opacity * 255.f)
            ));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // THE REST OF THE TOOL SLIDERS
        bool isEraser = app.isEraser();
        if (ImGui::Checkbox("Eraser Mode", &isEraser)) app.setEraser(isEraser);
        ImGui::Spacing();

        float size = app.getBrushSize();
        if (ImGui::SliderFloat("Size", &size, 1.f, 100.f)) app.setBrushSize(size);

        float smoothing = app.getBrushSmoothing();
        if (ImGui::SliderFloat("Smoothing", &smoothing, 0.f, 0.99f)) app.setBrushSmoothing(smoothing);

        float flow = app.getBrushFlow();
        if (ImGui::SliderFloat("Flow", &flow, 0.1f, 1.0f)) app.setBrushFlow(flow);

        float softness = app.getBrushSoftness();
        if (ImGui::SliderFloat("Softness", &softness, 0.0f, 1.0f, "%.2f")) app.setBrushSoftness(softness);

        ImGui::Spacing(); ImGui::Spacing();

        // --- AI ASSISTANT ---
        ImGui::TextDisabled("AI Co-Pilot");
        ImGui::Separator();
        bool aiEnabled = app.isAssistantEnabled();
        if (ImGui::Checkbox("Enable Assistant", &aiEnabled)) app.setAssistantEnabled(aiEnabled);

        if (aiEnabled && app.getAssistant()) {
            AssistantState state = app.getAssistant()->getState();

            ImGui::BeginChild("ChatHistory", ImVec2(0, 120), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            int msgIndex = 0;
            for (const auto& msg : app.getAssistant()->getChatHistory()) {
                if (msg.sender == "You") ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "You:");
                else ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "AI:");

                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 45.0f);
                if (ImGui::SmallButton(("Copy##" + std::to_string(msgIndex++)).c_str())) {
                    ImGui::SetClipboardText(msg.text.c_str());
                }

                ImGui::TextWrapped("%s", msg.text.c_str());
                ImGui::Separator();
            }
            if (state == AssistantState::Thinking) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "AI is typing...");
            else if (state == AssistantState::Error) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Error: %s", app.getAssistant()->getLastError().c_str());

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();

            static char chatInput[256] = "";
            ImGui::BeginDisabled(state == AssistantState::Thinking);
            ImGui::PushItemWidth(-50.0f);
            bool enterPressed = ImGui::InputText("##ChatInput", chatInput, sizeof(chatInput), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if ((ImGui::Button("Send", ImVec2(-1, 0)) || enterPressed) && strlen(chatInput) > 0) {
                app.getAssistant()->requestAIHelp(chatInput);
                chatInput[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
            ImGui::EndDisabled();
        }
        ImGui::Spacing(); ImGui::Spacing();

        // --- LAYERS PANEL ---
        ImGui::TextDisabled("Layer Stack");
        ImGui::Separator();
        if (ImGui::Button("New Layer", ImVec2(130, 0))) {
            if (app.getToolMode() == 8) app.setToolMode(0); // Force-commit the transform
            app.getCanvas().addLayer();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Folder", ImVec2(130, 0))) {
            if (app.getToolMode() == 8) app.setToolMode(0); // Force-commit the transform
            app.getCanvas().addFolder();
        }

        auto& layers = app.getCanvas().getLayers();
        int activeIdx = app.getCanvas().getActiveLayerIndex();
        const auto& selectedLayers = app.getCanvas().getSelectedLayers();

        if (activeIdx >= 0 && activeIdx < layers.size()) {
             const char* blendModeNames[] = { "Normal", "Multiply", "Add (Linear Dodge)", "Pass Through" };
             int currentBlendInt = static_cast<int>(layers[activeIdx]->blendMode);
             if (ImGui::Combo("Blend", &currentBlendInt, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {
                  app.getCanvas().beginBatchCommand();
                  for (int sel : selectedLayers) {
                     int finalBlend = currentBlendInt;
                     // Prevent 'Pass Through' on standard content layers (only folders use Pass Through)
                     if (layers[sel]->type == LayerType::Content && finalBlend == 3) finalBlend = 0;

                     app.getCanvas().pushUndoCommand(std::make_unique<BlendModeChangeCommand>(sel, static_cast<int>(layers[sel]->blendMode), finalBlend));
                     layers[sel]->blendMode = static_cast<LayerBlendMode>(finalBlend);
                  }
                  app.getCanvas().endBatchCommand();
             }
            float& currentOpacity = layers[activeIdx]->opacity;
            static std::map<int, float> initialOpacities;
            if (ImGui::SliderFloat("Opacity", &currentOpacity, 0.0f, 1.0f, "%.2f")) {
                for (int sel : selectedLayers) layers[sel]->opacity = currentOpacity;
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

        // --- LAYER LIST (DRAG & DROP) ---
        ImGui::BeginChild("LayerList", ImVec2(0, 0), true);
        ImGui::Selectable("--- Top of Stack ---", false, ImGuiSelectableFlags_Disabled);

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DRAG_AND_DROP")) {
                int draggedIndex = *(const int*)payload->Data;
                app.getCanvas().removeFromFolder(draggedIndex);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();

        // Store a paired struct: { layerIndex, visualIndentLevel }
        std::vector<std::pair<int, int>> uiOrder;
        std::vector<bool> processed(layers.size(), false);

        // Track the visual depth explicitly inside indentLevel, ignoring raw engine variables
        std::function<void(int, int, int, int)> buildUiOrder = [&](int startIdx, int endIdx, int targetDepth, int indentLevel) {
            for (int i = endIdx; i >= startIdx; --i) {
                if (!processed[i] && layers[i]->depth == targetDepth) {
                    processed[i] = true;
                    if (layers[i]->type == LayerType::Folder) {
                        uiOrder.push_back({ i, indentLevel });
                        int childStart = i + 1, childEnd = childStart;
                        while (childEnd <= endIdx && layers[childEnd]->depth > targetDepth) childEnd++;
                        // Increase the visual indentLevel by +1 for the children
                        if (childEnd > childStart) buildUiOrder(childStart, childEnd - 1, targetDepth + 1, indentLevel + 1);
                    }
                    else {
                        uiOrder.push_back({ i, indentLevel });
                    }
                }
            }
            };

        if (!layers.empty()) buildUiOrder(0, layers.size() - 1, 0, 0);
        for (int i = layers.size() - 1; i >= 0; --i) {
            if (!processed[i]) uiOrder.push_back({ i, 0 });
        }

        // Render the UI
        for (const auto& item : uiOrder) {
            int i = item.first;
            int visualIndent = item.second;

            ImGui::PushID(i);

            // Force a clean 20px indent strictly based on the UI tree hierarchy
            float indentSize = visualIndent * 20.0f;
            if (indentSize > 0) ImGui::Indent(indentSize);

            ImGui::Checkbox("##vis", &layers[i]->visible);
            ImGui::SameLine();

            // --- LOCK ICON ---
            ImGui::PushID(i + 999);

            // Solid white if locked, highly transparent (alpha 50) if unlocked
            sf::Color lockTint = layers[i]->isLocked ? sf::Color(255, 255, 255, 255) : sf::Color(255, 255, 255, 50);

            if (m_icons.count("lock")) {
                if (ImGui::ImageButton("lockBtn", m_icons["lock"], sf::Vector2f(14.f, 14.f), sf::Color::Transparent, lockTint)) {
                    layers[i]->isLocked = !layers[i]->isLocked;

                    // Safety: Drop transform if they lock the layer while actively transforming it
                    if (layers[i]->isLocked && app.getToolMode() == 8 && i == app.getCanvas().getActiveLayerIndex()) {
                        app.setToolMode(0);
                    }
                }
            }
            else {
                // Fallback text button just in case the image fails to load
                if (ImGui::Button(layers[i]->isLocked ? "[L]" : "[ ]", ImVec2(18, 18))) {
                    layers[i]->isLocked = !layers[i]->isLocked;
                }
            }
            ImGui::PopID();
            ImGui::SameLine();

            // --- THUMBNAIL CALCULATIONS ---
            float maxThumbSize = 28.0f; // Keeps the row height reasonable
            sf::Vector2u canvasSize = app.getCanvas().getSize();
            float canvasRatio = static_cast<float>(canvasSize.x) / static_cast<float>(canvasSize.y);

            float thumbW = maxThumbSize;
            float thumbH = maxThumbSize;
            // Proportional scaling so the thumbnail isn't stretched/distorted
            if (canvasRatio > 1.0f) thumbH = maxThumbSize / canvasRatio;
            else thumbW = maxThumbSize * canvasRatio;
            float yOffset = (maxThumbSize - thumbH) * 0.5f;

            std::string prefix = layers[i]->type == LayerType::Folder ? "[F] " : (visualIndent > 0 ? " \\_ " : "  ");
            if (layers[i]->isClipped) prefix += "-> ";

            bool isSelected = app.getCanvas().isLayerSelected(i);
            std::string selLabel = "##sel_" + std::to_string(i);

            // Record start position so the selectable and the custom content can be overlapped
            ImVec2 startPos = ImGui::GetCursorPos();

            // Draw the invisible Selectable box that captures the click
            if (ImGui::Selectable(selLabel.c_str(), isSelected, ImGuiSelectableFlags_AllowItemOverlap, ImVec2(ImGui::GetContentRegionAvail().x - 30.0f, maxThumbSize))) {
                if (app.getToolMode() == 8) app.setToolMode(0); // Force-commit the transform
                app.getCanvas().toggleLayerSelection(i, ImGui::GetIO().KeyCtrl);
            }

            // --- RIGHT-CLICK CONTEXT MENU ---
            if (ImGui::BeginPopupContextItem()) {
                if (!app.getCanvas().isLayerSelected(i)) {
                    if (app.getToolMode() == 8) app.setToolMode(0); // Force-commit the transform
                    app.getCanvas().toggleLayerSelection(i, false);
                }
                const auto& activeSelection = app.getCanvas().getSelectedLayers();

                ImGui::TextDisabled("Layer Actions");
                ImGui::Separator();

                if (layers[i]->type == LayerType::Content) {
                    bool allAlphaLocked = layers[i]->alphaLocked;
                    if (ImGui::Checkbox("Alpha Lock", &allAlphaLocked)) {
                        for (int sel : activeSelection) if (layers[sel]->type == LayerType::Content) layers[sel]->alphaLocked = allAlphaLocked;
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

                    if (ImGui::Selectable("Free Transform")) app.setToolMode(8);

                    if (i > 0 && layers[i - 1]->type == LayerType::Content) {
                        if (ImGui::Selectable("Merge Down")) layerToMergeDown = i;
                    }
                }

                if (layers[i]->type == LayerType::Folder && ImGui::Selectable("Merge Folder")) folderToMerge = i;

                ImGui::Separator();
                if (ImGui::Selectable("Rename Layer/Folder")) {
                    layerToRename = i;
#ifdef _MSC_VER
                    strncpy_s(renameBuffer, layers[i]->name.c_str(), sizeof(renameBuffer) - 1);
#else
                    strncpy(renameBuffer, layers[i]->name.c_str(), sizeof(renameBuffer) - 1);
#endif
                    renameBuffer[sizeof(renameBuffer) - 1] = '\0';
                }

                ImGui::Separator();
                if (ImGui::Selectable("Delete Selected")) {
                    std::vector<int> sortedSels(activeSelection.begin(), activeSelection.end());
                    std::sort(sortedSels.rbegin(), sortedSels.rend());
                    bool hasChildren = false;
                    for (int sel : sortedSels) {
                        if (layers[sel]->type == LayerType::Folder && sel + 1 < layers.size() && layers[sel + 1]->depth > layers[sel]->depth) hasChildren = true;
                    }
                    if (hasChildren) layerToDelete = i; else instantDeleteList = sortedSels;
                }
                ImGui::EndPopup();
            }

            // --- DRAG AND DROP ---
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("LAYER_DRAG_AND_DROP", &i, sizeof(int));
                ImGui::Text("Moving: %s", layers[i]->name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DRAG_AND_DROP")) {
                    int draggedIndex = *(const int*)payload->Data;
                    if (draggedIndex != i) {
                        if (app.getToolMode() == 8) app.setToolMode(0); // Force-commit the transform

                        if (layers[i]->type == LayerType::Folder) app.getCanvas().moveToFolder(draggedIndex, i);
                        else app.getCanvas().dropLayerToReorder(draggedIndex, i);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Move cursor back to draw our visual elements inside the box
            ImGui::SetCursorPos(startPos);

            // Draw Prefix
            ImGui::SetCursorPosY(startPos.y + (maxThumbSize - ImGui::GetTextLineHeight()) * 0.5f); // Center text
            ImGui::Text("%s", prefix.c_str());
            ImGui::SameLine();

            // Draw Thumbnail
            ImGui::SetCursorPosY(startPos.y + yOffset);
            if (layers[i]->type == LayerType::Content) {

                // RECALCULATE BOUNDS (Only if the layer was modified)
                if (layers[i]->boundsDirty) {
                    sf::Image img = layers[i]->texture->getTexture().copyToImage();
                    sf::Vector2u size = img.getSize();
                    const uint8_t* pixels = img.getPixelsPtr();

                    int minX = size.x, minY = size.y, maxX = 0, maxY = 0;
                    bool found = false;

                    for (unsigned int y = 0; y < size.y; ++y) {
                        for (unsigned int x = 0; x < size.x; ++x) {
                            if (pixels[(y * size.x + x) * 4 + 3] > 0) { // If Alpha > 0
                                if ((int)x < minX) minX = x;
                                if ((int)x > maxX) maxX = x;
                                if ((int)y < minY) minY = y;
                                if ((int)y > maxY) maxY = y;
                                found = true;
                            }
                        }
                    }

                    if (found) {
                        // Add a 5px padding so the paint doesn't touch the exact edge of the thumbnail box
                        minX = std::max(0, minX - 5);
                        minY = std::max(0, minY - 5);
                        maxX = std::min((int)size.x - 1, maxX + 5);
                        maxY = std::min((int)size.y - 1, maxY + 5);
                        layers[i]->contentBounds = sf::IntRect(sf::Vector2i(minX, minY), sf::Vector2i(maxX - minX, maxY - minY));
                    }
                    else {
                        layers[i]->contentBounds = sf::IntRect(sf::Vector2i(0, 0), sf::Vector2i(0, 0)); // Empty layer
                    }

                    layers[i]->boundsDirty = false; // Turn the flag off so it doesn't lag
                }

                sf::IntRect bounds = layers[i]->contentBounds;

                if (bounds.size.x <= 0 || bounds.size.y <= 0) {
                    // Empty Layer: Draw transparent box
                    // Pass a negative matrix to flip the empty texture
                    sf::Sprite emptySprite(layers[i]->texture->getTexture());
                    sf::Vector2i tSize(layers[i]->texture->getSize().x, layers[i]->texture->getSize().y);
                    emptySprite.setTextureRect(sf::IntRect(sf::Vector2i(0, tSize.y), sf::Vector2i(tSize.x, -tSize.y)));

                    ImGui::Image(emptySprite, sf::Vector2f(thumbW, thumbH), sf::Color::White, sf::Color(120, 120, 120, 150));
                }
                else {
                    // Cropped Thumbnail
                    float boxRatio = static_cast<float>(bounds.size.x) / static_cast<float>(bounds.size.y);
                    float renderW = maxThumbSize;
                    float renderH = maxThumbSize;

                    if (boxRatio > 1.0f) renderH = maxThumbSize / boxRatio;
                    else renderW = maxThumbSize * boxRatio;

                    ImGui::SetCursorPosY(startPos.y + (maxThumbSize - renderH) * 0.5f + 2.0f);

                    //Invert the UV Coordinates to un-flip the cropped image
                    sf::Sprite thumbSprite(layers[i]->texture->getTexture());

                    sf::IntRect flippedBounds;

                    flippedBounds.position = sf::Vector2i(bounds.position.x, bounds.position.y + bounds.size.y);
                    flippedBounds.size = sf::Vector2i(bounds.size.x, -bounds.size.y);

                    thumbSprite.setTextureRect(flippedBounds);
                    ImGui::Image(thumbSprite, sf::Vector2f(renderW, renderH), sf::Color::White, sf::Color(120, 120, 120, 150));
                }
            }
            else {
                // For folders, draw an empty dummy box to keep spacing aligned
                ImGui::Dummy(ImVec2(maxThumbSize, maxThumbSize));
            }
            ImGui::SameLine();

            // Draw Layer Name
            std::string label = layers[i]->name;
            if (label.length() > 14){
                // Manually truncate the name to 14 characters and add elipses
                label = label.substr(0, 14) + "...";
            }
            ImGui::TextUnformatted(label.c_str());

            // Draw [A] if Alpha Locked
            if (layers[i]->alphaLocked) {
                ImGui::SameLine();
                ImGui::TextDisabled("[A]");
            }

            // Move the cursor down to the end of the block so the next layer renders below
            ImGui::SetCursorPosY(startPos.y + maxThumbSize + 4.0f);

            // Unindent at the end of the layer to prevent leakage
            if (indentSize > 0) ImGui::Unindent(indentSize);

            ImGui::PopID();
        }
        ImGui::EndChild(); // End Layer List

        ImGui::PopItemWidth();
        ImGui::End(); // End Right Properties Panel


        // ==========================================
        //  BACKGROUND MODALS & DEFERRED ACTIONS
        // ==========================================
        if (!instantDeleteList.empty()) {
            app.getCanvas().beginBatchCommand();
            for (int idx : instantDeleteList) app.getCanvas().deleteLayer(idx);
            app.getCanvas().endBatchCommand();
            instantDeleteList.clear();
        }
        if (layerToMergeDown != -1) { app.getCanvas().mergeDown(layerToMergeDown); layerToMergeDown = -1; }
        if (folderToMerge != -1) { app.getCanvas().mergeFolder(folderToMerge); folderToMerge = -1; }

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
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { layerToRename = -1; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        else if (layerToRename != -1) layerToRename = -1;

        // Delete Modal
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete Folder Warning", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("This folder contains other layers.\nDeleting it will permanently remove EVERYTHING inside it!");
            ImGui::Separator(); ImGui::Spacing();
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
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { layerToDelete = -1; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        else if (layerToDelete != -1) layerToDelete = -1;

        // Canvas Background Context Menu
        if (app.getCanvas().hasSelection() && ImGui::BeginPopupContextVoid("CanvasContextMenu")) {
            ImGui::TextDisabled("Selection Options");
            ImGui::Separator();

            if (ImGui::Selectable("Copy to New Layer")) {
                app.getCanvas().copySelectionToNewLayer();
            }
            if (ImGui::Selectable("Cut to New Layer")) {
                app.getCanvas().cutSelectionToNewLayer();
            }
            ImGui::Separator();

            if (ImGui::Selectable("Delete Selected Area")) app.getCanvas().clearSelectionOnSelectedLayers();
            if (ImGui::Selectable("Deselect")) {
                app.getCanvas().getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
                app.getCanvas().setSelectionActive(false);
            }
            ImGui::EndPopup();
        }
    }
    // ==========================================
    //  GLOBAL MODALS
    // ==========================================
    
    // --- EXIT WARNING MODAL ---
    if (app.shouldShowExitWarning()) {
        ImGui::OpenPopup("Unsaved Changes");
        app.cancelExit(); // Consume the trigger so it only fires the popup once
    }

    ImVec2 screenCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(screenCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Changes", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("You have unsaved changes in your project!\nAre you sure you want to exit?");
        ImGui::Separator(); ImGui::Spacing();

        if (ImGui::Button("Save and Exit", ImVec2(130, 0))) {
            // If the save is successful (or already exists), force the close.
            // If they hit 'Cancel' in the Windows File Explorer, the modal stays open
            if (app.saveCurrentProject()) {
                app.forceExit();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit Without Saving", ImVec2(160, 0))) {
            app.forceExit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    static char domainBuffer[256] = ""; // The buffer for the text input
    // --- SETTINGS MODAL ---
    if (openSettingsModal) {
        ImGui::OpenPopup("App Settings");
        openSettingsModal = false;
        // Sync the UI buffer with the internal App memory right as the window opens
            std::string currentDomain = app.getApiDomain();
#ifdef _MSC_VER
        strncpy_s(domainBuffer, currentDomain.c_str(), sizeof(domainBuffer) - 1);
#else
        strncpy(domainBuffer, currentDomain.c_str(), sizeof(domainBuffer) - 1);
#endif
        domainBuffer[sizeof(domainBuffer) - 1] = '\0';
    }
    screenCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(screenCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("App Settings", NULL, ImGuiWindowFlags_NoResize)) {

        if (ImGui::BeginTabBar("SettingsTabs")) {

            // ==========================================
            // TAB 1: SYSTEM SETTINGS
            // ==========================================
            if (ImGui::BeginTabItem("System")) {
                ImGui::Spacing();

                // --- THEMES ---
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Visual Theme");
                ImGui::Separator(); ImGui::Spacing();

                int currentTheme = app.getTheme();
                const char* themes[] = { "Dark Mode (Default)", "Light Mode", "Classic ImGui" };
                if (ImGui::Combo("Theme", &currentTheme, themes, IM_ARRAYSIZE(themes))) {
                    app.setTheme(currentTheme);
                }
                ImGui::Spacing(); ImGui::Spacing();

                // --- DYNAMIC SHORTCUTS ---
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Keyboard Shortcuts");
                ImGui::Separator(); ImGui::Spacing();

                // Fixed height so it leaves room for the buttons at the bottom!
                ImGui::BeginChild("ShortcutPane", ImVec2(0, 240), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                auto& shortcuts = app.getShortcuts();
                for (auto& pair : shortcuts) {
                    ImGui::Text("%s", pair.first.c_str());
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 180.0f);

                    std::string rebindAction = app.getActionToRebind();
                    if (rebindAction == pair.first) {
                        // ## used to attach the action name as an invisible, 100% unique ID
                        ImGui::Button("Press any key... (Esc)##", ImVec2(160, 0));
                    }
                    else {
                        std::string currentKey = app.getShortcutString(pair.first);
                        std::string label = currentKey + "##" + pair.first;
                        if (ImGui::Button(label.c_str(), ImVec2(160, 0))) {
                            app.setActionToRebind(pair.first);
                        }
                    }
                    ImGui::Separator();
                }
                ImGui::EndChild();

                ImGui::Spacing();
                if (ImGui::Button("Restore System Defaults", ImVec2(200, 0))) {
                    app.setTheme(0); // Restore Dark Mode
                    app.restoreDefaultShortcuts(); // Restore keybinds
                    app.setActionToRebind(""); // Cancel any active rebinding
                }

                ImGui::EndTabItem();
            }

            // ==========================================
            // TAB 2: TABLET SETTINGS
            // ==========================================
            if (ImGui::BeginTabItem("Tablet")) {
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tablet Pen Pressure");
                ImGui::Separator(); ImGui::Spacing();

                float curve = app.getPressureCurve();

                // Mathematically generate 100 points to draw the visual graph
                float graphValues[100];
                for (int i = 0; i < 100; ++i) {
                    float x = i / 99.0f;
                    graphValues[i] = std::pow(x, curve);
                }

                ImGui::PlotLines("##PressureGraph", graphValues, 100, 0, NULL, 0.0f, 1.0f, ImVec2(0, 150));
                ImGui::Spacing();

                if (ImGui::SliderFloat("Pressure Curve", &curve, 0.1f, 3.0f, "Gamma: %.2f")) {
                    app.setPressureCurve(curve);
                }
                ImGui::TextDisabled("Lower = Softer | 1.0 = Linear | Higher = Harder");

                ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

                if (ImGui::Button("Restore Tablet Defaults", ImVec2(200, 0))) {
                    app.setPressureCurve(1.0f); // Restore Linear curve
                }

                ImGui::EndTabItem();
            }
            // ==========================================
            // TAB 3: AI SETUP
            // ==========================================
            if (ImGui::BeginTabItem("AI Setup")) {
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Cloud GPU Connection");
                ImGui::Separator(); ImGui::Spacing();

                ImGui::Text("Cloudflare / Ngrok URL:");
                ImGui::PushItemWidth(-20.0f); // Make the input box stretch to fit
                if (ImGui::InputText("##ApiDomain", domainBuffer, sizeof(domainBuffer))) {
                    // Update the application memory dynamically as the user types
                    app.setApiDomain(domainBuffer);
                }
                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::TextDisabled("Example: random-words.trycloudflare.com");
                ImGui::TextDisabled("Do not include https:// or trailing slashes.");

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // ==========================================
        //  CLOSE BUTTON
        // ==========================================
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Close Settings", ImVec2(120, 0))) {
            app.setActionToRebind("");
            app.saveSettings();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

bool ImGuiLayer::wantsCaptureKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}