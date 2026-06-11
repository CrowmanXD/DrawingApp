#include "Application.h"
#include "ClipboardHelper.h"
#include "MockAssistant.h"
#include "LLMAssistant.h"
#include "FileDialogs.h"
#include <fstream>

#ifdef _WIN32
#include <windows.h>

static WNDPROC g_originalWndProc = nullptr;
static Application* g_appInstance = nullptr;

// A Window Subclass that listens to Windows hardware events. If it detects a Pen, it reads the 1024-level pressure sensor. 
// If it detects a physical Mouse, it maxes the pressure out to 1.0f
LRESULT CALLBACK TabletProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 0x0245 = WM_POINTERUPDATE, 0x0246 = WM_POINTERDOWN
    if (msg == 0x0245 || msg == 0x0246) {
        UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE pointerType;

        if (GetPointerType(pointerId, &pointerType)) {
            if (pointerType == PT_PEN) {
                POINTER_PEN_INFO penInfo;
                if (GetPointerPenInfo(pointerId, &penInfo)) {
                    // Hardware pressure is typically 0 to 1024. Normalize it to 0.0 -> 1.0
                    float pressure = static_cast<float>(penInfo.pressure) / 1024.0f;
                    if (g_appInstance) g_appInstance->setPenPressure(pressure);
                }
            }
            else if (pointerType == PT_MOUSE) {
                // If the user grabs their physical mouse, force pressure to 100%
                if (g_appInstance) g_appInstance->setPenPressure(1.0f);
            }
        }
    }
    // Pass the message back to SFML so the rest of the engine keeps working normally
    return CallWindowProc(g_originalWndProc, hwnd, msg, wParam, lParam);
}
#endif

Application::Application()
    : m_window(sf::VideoMode::getDesktopMode(), "Licenta Desen C++", sf::Style::Default), 
    m_colorPickerTool(*this)
{
    m_window.setFramerateLimit(60);
    m_imgui.init(m_window);
    m_activeTool = &m_brushTool;
    initShortcuts();

    // --- ATTACH THE TABLET HOOK ---
#ifdef _WIN32
    g_appInstance = this;
    HWND hwnd = (HWND)m_window.getNativeHandle();
    g_originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)TabletProc);
#endif

    // --- COMPILE THE EDGE-DETECTION MARCHING ANTS SHADER ---
    if (sf::Shader::isAvailable()) {
        try {
            const std::string antsCode = R"(
                uniform sampler2D selectionMask;
                uniform vec2 textureSize;
                uniform float time;

                void main() {
                    vec2 uv = gl_TexCoord[0].xy;
                    float center = texture2D(selectionMask, uv).a;
                    
                    float dx = 1.0 / textureSize.x;
                    float dy = 1.0 / textureSize.y;
                    
                    float left = texture2D(selectionMask, uv + vec2(-dx, 0.0)).a;
                    float right = texture2D(selectionMask, uv + vec2(dx, 0.0)).a;
                    float top = texture2D(selectionMask, uv + vec2(0.0, dy)).a;
                    float bottom = texture2D(selectionMask, uv + vec2(0.0, -dy)).a;
                    
                    // Edge Detection: If it's solid, but any neighbor is transparent, we are on the border!
                    if (center > 0.5 && (left < 0.5 || right < 0.5 || top < 0.5 || bottom < 0.5)) {
                        float dash = sin((gl_FragCoord.x + gl_FragCoord.y) * 0.5 - time * 15.0);
                        if (dash > 0.0) gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
                        else gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                    } else {
                        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
                    }
                }
            )";
            m_antsShader.loadFromMemory(antsCode, sf::Shader::Type::Fragment);
        }
        catch (...) {}
    }
}

Application::~Application() {
    // --- DETACH THE TABLET HOOK ---
#ifdef _WIN32
    HWND hwnd = (HWND)m_window.getNativeHandle();
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
#endif
    m_imgui.shutdown();
}

void Application::initiateExit() {
    // If the canvas exists AND has unsaved changes, trigger the warning modal
    if (m_canvas && m_canvas->isDirty()) {
        m_showExitWarning = true;
    }
    else {
        forceExit();
    }
}

void Application::forceExit() {
    m_window.close();
    m_running = false;
}

void Application::cancelExit() {
    m_showExitWarning = false;
}

void Application::initShortcuts() {
    restoreDefaultShortcuts(); // Load the defaults
    loadSettings();            // Overwrite them with the user's saved file
}

void Application::restoreDefaultShortcuts() {
    m_shortcuts.clear();

    m_shortcuts["File: Save"] = { sf::Keyboard::Key::S, true, false, false }; // Ctrl+S

    // Standard Edit Commands
    m_shortcuts["Edit: Undo"] = { sf::Keyboard::Key::Z, true, false, false }; // Ctrl+Z
    m_shortcuts["Edit: Redo"] = { sf::Keyboard::Key::Z, true, true, false };  // Ctrl+Shift+Z
    m_shortcuts["Edit: Copy"] = { sf::Keyboard::Key::C, true, false, false }; // Ctrl+C
    m_shortcuts["Edit: Cut"] = { sf::Keyboard::Key::X, true, false, false };  // Ctrl+X
    m_shortcuts["Edit: Paste"] = { sf::Keyboard::Key::V, true, false, false };// Ctrl+V
    m_shortcuts["Image: Mirror"] = { sf::Keyboard::Key::M, false, false, false };

    // Tool Selection Shortcuts
    m_shortcuts["Tool: Brush"] = { sf::Keyboard::Key::B, false, false, false };
    m_shortcuts["Tool: Rect Select"] = { sf::Keyboard::Key::R, false, false, false };
    m_shortcuts["Tool: Sel. Brush"] = { sf::Keyboard::Key::W, false, false, false };
    m_shortcuts["Tool: Pen"] = { sf::Keyboard::Key::P, false, false, false };
    m_shortcuts["Tool: Lasso"] = { sf::Keyboard::Key::L, false, false, false };
    m_shortcuts["Tool: Paint Bucket"] = { sf::Keyboard::Key::G, false, false, false };
    m_shortcuts["Tool: Shapes"] = { sf::Keyboard::Key::U, false, false, false };
    m_shortcuts["Tool: Crop"] = { sf::Keyboard::Key::C, false, false, false };
    m_shortcuts["Tool: Transform"] = { sf::Keyboard::Key::T, false, false, false };
    m_shortcuts["Tool: Color Picker"] = { sf::Keyboard::Key::I, false, false, false };
    m_shortcuts["Tool: Toggle Eraser"] = { sf::Keyboard::Key::E, false, false, false };

    // Zooming (Defaults to + and -)
    m_shortcuts["View: Zoom In"] = { sf::Keyboard::Key::Equal, false, false, false };
    m_shortcuts["View: Zoom Out"] = { sf::Keyboard::Key::Hyphen, false, false, false };

    // Brush Sizing (Defaults to [ and ])
    m_shortcuts["Brush: Increase Size"] = { sf::Keyboard::Key::RBracket, false, false, false };
    m_shortcuts["Brush: Decrease Size"] = { sf::Keyboard::Key::LBracket, false, false, false };
}

void Application::run() {
    sf::Clock clock;

    while (m_running && m_window.isOpen()) {
        sf::Time deltaTime = clock.restart();

        processEvents();
        update(deltaTime);
        render();
    }
}

sf::Vector2f Application::getCanvasOffset() const {
    if (!m_canvas) return { 0.f, 0.f };

    // Define the exact sizes of the UI panels
    float leftUIWidth = 70.f;
    float rightUIWidth = 300.f;
    float topMenuHeight = 24.f; // The height of the Main Menu Bar

    float workspaceWidth = static_cast<float>(m_window.getSize().x) - leftUIWidth - rightUIWidth;
    float workspaceHeight = static_cast<float>(m_window.getSize().y) - topMenuHeight;

    float scaledWidth = static_cast<float>(m_canvas->getSize().x) * m_zoom;
    float scaledHeight = static_cast<float>(m_canvas->getSize().y) * m_zoom;

    // Center the canvas dynamically in the gap
    float offsetX = leftUIWidth + (workspaceWidth - scaledWidth) / 2.f + m_pan.x;
    float offsetY = topMenuHeight + (workspaceHeight - scaledHeight) / 2.f + m_pan.y;

    return { offsetX, offsetY };
}

void Application::startDrawing(unsigned int width, unsigned int height) {
    m_canvas = std::make_unique<Canvas>(sf::Vector2u(width, height));

    // Clear the new canvas to transparent black to avoid dark halos
    m_canvas->clear(sf::Color(0, 0, 0, 0));

    m_assistant = std::make_unique<AssistantController>(*m_canvas);

    // This expects a local AI server running on port 1234.
    m_assistant->setBackend(std::make_unique<LLMAssistant>("localhost", 1234));
    m_state = AppState::DrawingEditor;
}

void Application::processEvents() {
    while (const std::optional event = m_window.pollEvent()) {
        m_imgui.processEvent(m_window, *event);

        if (event->is<sf::Event::Closed>()) {
            initiateExit();
        }

        // --- HANDLE WINDOW RESIZING ---
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            // Update the view to the new size so the canvas doesn't stretch
            sf::FloatRect visibleArea({ 0.f, 0.f }, { static_cast<float>(resized->size.x), static_cast<float>(resized->size.y) });
            m_window.setView(sf::View(visibleArea));
        }

        // --- GLOBAL KEYBOARD INTERCEPTOR ---
		// Placed outside the DrawingEditor state check, so it works in the Main Menu for rebinding shortcuts
        if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
            if (!m_actionToRebind.empty()) {
                if (key->code == sf::Keyboard::Key::Escape) {
                    m_actionToRebind = ""; // Cancel the rebind
                }
                else if (key->code != sf::Keyboard::Key::LControl && key->code != sf::Keyboard::Key::RControl &&
                    key->code != sf::Keyboard::Key::LShift && key->code != sf::Keyboard::Key::RShift &&
                    key->code != sf::Keyboard::Key::LAlt && key->code != sf::Keyboard::Key::RAlt &&
                    key->code != sf::Keyboard::Key::Unknown) {

                    // Save the combo and end the listening state
                    m_shortcuts[m_actionToRebind] = { key->code, key->control, key->shift, key->alt };
                    m_actionToRebind = "";
                }
                continue; // Stop the rest of the engine from processing this key
            }
        }

        if (m_state == AppState::DrawingEditor && m_canvas) {

            // Handle keyboard shortcuts
            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {

                // --- THE SHORTCUT INTERCEPTOR ---
                // If a rebind is actively happening, swallow the keypress
                if (!m_actionToRebind.empty()) {
                    if (key->code == sf::Keyboard::Key::Escape) {
                        m_actionToRebind = ""; // Cancel the rebind
                    }
                    // Ignore raw modifier keys (wait for them to press the actual letter)
                    else if (key->code != sf::Keyboard::Key::LControl && key->code != sf::Keyboard::Key::RControl &&
                        key->code != sf::Keyboard::Key::LShift && key->code != sf::Keyboard::Key::RShift &&
                        key->code != sf::Keyboard::Key::LAlt && key->code != sf::Keyboard::Key::RAlt &&
                        key->code != sf::Keyboard::Key::Unknown) {

                        // Save the exact combo and end the listening state
                        m_shortcuts[m_actionToRebind] = { key->code, key->control, key->shift, key->alt };
                        m_actionToRebind = "";
                    }
                    continue; // Stop the rest of the engine from processing this key
                }

                // --- PASS TO ACTIVE TOOL ---
                m_activeTool->onKeyPress(*m_canvas, key->code);

                // --- EXECUTE DYNAMIC EDIT SHORTCUTS ---
                if (isShortcutPressed("File: Save", key)) {
                    saveCurrentProject();
                }
                else if (isShortcutPressed("Edit: Undo", key)) {
                    if (m_currentToolMode == 8 && m_transformTool.isActive()) m_transformTool.onKeyPress(*m_canvas, sf::Keyboard::Key::Escape);
                    else m_canvas->undo();
                }
                else if (isShortcutPressed("Edit: Redo", key)) {
                    if (m_currentToolMode != 8 || !m_transformTool.isActive()) m_canvas->redo();
                }
                else if (isShortcutPressed("Edit: Paste", key)) {
                    sf::Image clipboardImg = ClipboardHelper::getImage();
                    if (clipboardImg.getSize().x > 0) m_canvas->importFromImage(clipboardImg, "Pasted Layer");
                }
                else if (isShortcutPressed("Edit: Copy", key)) m_canvas->copyToClipboard();
                else if (isShortcutPressed("Edit: Cut", key)) m_canvas->cutToClipboard();
                else if (isShortcutPressed("Image: Mirror", key)) {
                    if (m_currentToolMode != 8 || !m_transformTool.isActive()) m_canvas->flipCanvasHorizontal();
                }
                else if (isShortcutPressed("Tool: Toggle Eraser", key)) {
                    setEraser(!isEraser());
                }
                else if (isShortcutPressed("View: Zoom In", key)) {
                    m_zoom = std::clamp(m_zoom * 1.1f, 0.1f, 10.0f);
                }
                else if (isShortcutPressed("View: Zoom Out", key)) {
                    m_zoom = std::clamp(m_zoom / 1.1f, 0.1f, 10.0f);
                }
                else if (isShortcutPressed("Brush: Increase Size", key)) {
                    float size = getBrushSize();
                    // Stepped math: Increase by 5 if big, 2 if medium, 1 if small
                    float inc = (size >= 20.f) ? 5.f : ((size >= 5.f) ? 2.f : 1.f);
                    setBrushSize(std::min(100.f, size + inc));
                }
                else if (isShortcutPressed("Brush: Decrease Size", key)) {
                    float size = getBrushSize();
                    // Stepped math: Decrease by 5 if big, 2 if medium, 1 if small
                    float dec = (size > 20.f) ? 5.f : ((size > 5.f) ? 2.f : 1.f);
                    setBrushSize(std::max(1.f, size - dec));
                }

                // --- DYNAMIC TOOL SELECTION SHORTCUTS ---
                if (!m_imgui.wantsCaptureKeyboard()) { // Don't change tools while typing layer names
                    if (isShortcutPressed("Tool: Brush", key)) setToolMode(0);
                    else if (isShortcutPressed("Tool: Rect Select", key)) setToolMode(1);
                    else if (isShortcutPressed("Tool: Sel. Brush", key)) setToolMode(2);
                    else if (isShortcutPressed("Tool: Pen", key)) setToolMode(3);
                    else if (isShortcutPressed("Tool: Lasso", key)) setToolMode(4);
                    else if (isShortcutPressed("Tool: Paint Bucket", key)) setToolMode(5);
                    else if (isShortcutPressed("Tool: Shapes", key)) setToolMode(6);
                    else if (isShortcutPressed("Tool: Crop", key)) setToolMode(7);
                    else if (isShortcutPressed("Tool: Transform", key)) setToolMode(8);
                    else if (isShortcutPressed("Tool: Color Picker", key)) setToolMode(9);
                }
            }

            if (!m_imgui.wantsCaptureMouse()) {
                sf::Vector2f offset = getCanvasOffset();
                sf::Vector2u canvasSize = m_canvas->getSize();

                // --- ZOOMING (Scroll Wheel) ---
                if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>()) {
                    if (scroll->wheel == sf::Mouse::Wheel::Vertical) {
                        float zoomFactor = 1.1f; // 10% zoom per scroll click
                        if (scroll->delta > 0) m_zoom *= zoomFactor;
                        else if (scroll->delta < 0) m_zoom /= zoomFactor;

                        // Prevent zooming infinitely far in or out
                        m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
                    }
                }

                // --- BUILD THE UNIVERSAL CAMERA ---
                sf::Transform camera;
                sf::Vector2f screenCenter(m_window.getSize().x / 2.f, m_window.getSize().y / 2.f);

                camera.translate(screenCenter);
                camera.rotate(sf::degrees(m_workspaceRotation));
                camera.translate(-screenCenter);

                camera.translate(offset);
                camera.scale(sf::Vector2f(m_zoom, m_zoom));

                // --- MOUSE DOWN ---
                if (const auto* mousePress = event->getIf<sf::Event::MouseButtonPressed>()) {
                    if (mousePress->button == sf::Mouse::Button::Middle) {
                        m_isPanning = true;
                        m_lastMousePos = mousePress->position;
                    }
                    else {
                        // Use Camera Inverse for Clicks
                        sf::Vector2f mousePos(static_cast<float>(mousePress->position.x), static_cast<float>(mousePress->position.y));
                        sf::Vector2f canvasPos = camera.getInverse().transformPoint(mousePos);

                        if (mousePress->button == sf::Mouse::Button::Right) {
                            m_activeTool->onRightClick(*m_canvas, canvasPos);
                        }
                        else if (mousePress->button == sf::Mouse::Button::Left) {
                            bool isCropOrTransform = (m_currentToolMode == 7 || m_currentToolMode == 8);
                            bool isInsideCanvas = (canvasPos.x >= 0.f && canvasPos.x <= canvasSize.x &&
                                canvasPos.y >= 0.f && canvasPos.y <= canvasSize.y);
                            bool isValidLayer = (m_canvas->getActiveLayer()->type != LayerType::Folder && !m_canvas->getActiveLayer()->isLocked);

                            if (isCropOrTransform || (isValidLayer && isInsideCanvas)) {
                                if (!isCropOrTransform) m_canvas->beginStroke();
                                m_activeTool->setPenPressure(m_penPressure);
                                m_activeTool->onMouseDown(*m_canvas, canvasPos);
                            }
                        }
                    }
                }

                // --- MOUSE MOVE ---
                if (const auto* mouseMove = event->getIf<sf::Event::MouseMoved>()) {
                    if (m_isPanning) {
                        sf::Vector2i delta = mouseMove->position - m_lastMousePos;

                        // Counter-rotate the mouse drag
                        sf::Transform invRot;
                        invRot.rotate(sf::degrees(-m_workspaceRotation));
                        sf::Vector2f panDelta = invRot.transformPoint(sf::Vector2f(static_cast<float>(delta.x), static_cast<float>(delta.y)));

                        m_pan.x += panDelta.x;
                        m_pan.y += panDelta.y;
                        m_lastMousePos = mouseMove->position;
                    }
                    else {
                        sf::Vector2f mousePos(static_cast<float>(mouseMove->position.x), static_cast<float>(mouseMove->position.y));
                        sf::Vector2f canvasPos = camera.getInverse().transformPoint(mousePos);
                        m_activeTool->setPenPressure(m_penPressure);
                        m_activeTool->onMouseMove(*m_canvas, canvasPos);
                    }
                }

                // --- MOUSE UP ---
                if (const auto* mouseRelease = event->getIf<sf::Event::MouseButtonReleased>()) {
                    if (mouseRelease->button == sf::Mouse::Button::Middle) {
                        m_isPanning = false;
                    }
                    else if (mouseRelease->button == sf::Mouse::Button::Left) {
                        // Use Camera Inverse for Releases
                        sf::Vector2f mousePos(static_cast<float>(mouseRelease->position.x), static_cast<float>(mouseRelease->position.y));
                        sf::Vector2f canvasPos = camera.getInverse().transformPoint(mousePos);

                        m_activeTool->onMouseUp(*m_canvas, canvasPos);

                        if (m_currentToolMode != 7 && m_currentToolMode != 8) {
                            m_canvas->endStroke();
                        }
                    }
                }
            }
        }
    }
}

void Application::update(sf::Time deltaTime) {
    m_imgui.update(m_window, deltaTime, *this);

    // Process any finished AI tasks safely on the main thread
    if (m_assistant && m_assistantEnabled) {
        m_assistant->processPendingActions();
    }
}

void Application::render() {
    // Clear the window to your dark gray UI background
    m_window.clear(sf::Color(40, 40, 40));

    if (m_state == AppState::DrawingEditor && m_canvas) {
        sf::Vector2f offset = getCanvasOffset();

        // --- BUILD THE UNIVERSAL CAMERA ---
        sf::Transform camera;
        sf::Vector2f screenCenter(m_window.getSize().x / 2.f, m_window.getSize().y / 2.f);

        camera.translate(screenCenter);
        camera.rotate(sf::degrees(m_workspaceRotation));
        camera.translate(-screenCenter);

        camera.translate(offset);
        camera.scale(sf::Vector2f(m_zoom, m_zoom));

        // --- DRAW PAPER BACKGROUND ---
        sf::RectangleShape paperBg(sf::Vector2f(m_canvas->getSize()));
        paperBg.setFillColor(sf::Color::White);

        m_window.draw(paperBg, camera);

        // --- RENDER COMPOSITE LAYER ---
        m_canvas->renderComposite();
        sf::Sprite compSprite(m_canvas->getCompositeTexture());

        sf::BlendMode premultipliedBlend(
            sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
            sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
        );

        // Combine the special Blend Mode and the Camera Tilt into one RenderState
        sf::RenderStates compStates(premultipliedBlend);
        compStates.transform = camera;
        m_window.draw(compSprite, compStates);

        // --- DRAW THE EDGE-DETECTED SELECTION MASK ---
        if (m_canvas->hasSelection() || m_canvas->isSelectionLive()) {
            if (sf::Shader::isAvailable()) {
                try {
                    m_antsShader.setUniform("selectionMask", m_canvas->getSelectionTextureConst());
                    m_antsShader.setUniform("textureSize", sf::Vector2f(static_cast<float>(m_canvas->getSize().x), static_cast<float>(m_canvas->getSize().y)));
                    m_antsShader.setUniform("time", m_antsClock.getElapsedTime().asSeconds());

                    sf::Sprite antsOverlay(m_canvas->getSelectionTextureConst());

                    // Combine the Shader and the Camera Tilt
                    sf::RenderStates antsStates(&m_antsShader);
                    antsStates.transform = camera;
                    m_window.draw(antsOverlay, antsStates);
                }
                catch (...) {}
            }
        }

        // --- DRAW TOOL OVERLAYS (Handles, Nodes, Bounding Boxes) ---
        if (m_activeTool) {
            m_activeTool->onDrawOverlay(*m_canvas, m_window, offset, m_zoom, m_workspaceRotation);
        }
        // --- DYNAMIC BRUSH CURSOR ---
        // Modes: 0 (Brush/Eraser), 2 (Selection Brush), 3 (Vector Pen)
        bool isBrushTool = (m_currentToolMode == 0 || m_currentToolMode == 2 || m_currentToolMode == 3);

        // Only draw the circle if using a brush and not hovering over the ImGui panels
        if (isBrushTool && !m_imgui.wantsCaptureMouse()) {
            m_window.setMouseCursorVisible(false); // Hide standard OS arrow

            // Grab exact screen-space mouse position
            sf::Vector2i mousePos = sf::Mouse::getPosition(m_window);

            // Calculate physical screen radius based on camera zoom
            float radius = (getBrushSize() / 2.0f) * m_zoom;

            sf::CircleShape cursor(radius);
            cursor.setOrigin(sf::Vector2f(radius, radius));
            cursor.setPosition(sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)));
            cursor.setFillColor(sf::Color::Transparent);

            // Dynamically increase polygon count so massive brushes stay perfectly round
            cursor.setPointCount(static_cast<std::size_t>(std::max(30.0f, radius * 2.0f)));

            // Draw Outer Black Outline
            cursor.setOutlineThickness(1.0f);
            cursor.setOutlineColor(sf::Color(0, 0, 0, 150));
            m_window.draw(cursor);

            // Draw Inner White Outline (Only if radius is large enough)
            // This ensures the cursor is always visible on both pitch-black and pure-white paintings
            if (radius > 1.0f) {
                cursor.setOutlineThickness(-1.0f);
                cursor.setOutlineColor(sf::Color(255, 255, 255, 200));
                m_window.draw(cursor);
            }

            // Draw a tiny precision crosshair dot in the absolute center
            sf::RectangleShape dot(sf::Vector2f(2.f, 2.f));
            dot.setOrigin(sf::Vector2f(1.f, 1.f));
            dot.setPosition(sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)));
            dot.setFillColor(sf::Color(0, 0, 0, 200));
            m_window.draw(dot);

        }
        else {
           // Restore the standard OS cursor if hovering the UI or using tools like Transform/Paint Bucket
            m_window.setMouseCursorVisible(true);
        }
    }
    // Draw UI
    m_imgui.render(m_window);
    m_window.display();
}

sf::Color Application::getBrushColor() const { return m_brushTool.getColor(); }

void Application::setBrushColor(const sf::Color& color) {
    m_brushTool.setColor(color);
    m_penTool.setColor(color);
    m_paintBucketTool.setColor(color);
    m_shapeTool.setColor(color);
}

float Application::getBrushSize() const {
    if (m_currentToolMode == 2) return m_selectionBrushTool.getSize();
    if (m_currentToolMode == 3) return m_penTool.getSize();
    return m_brushTool.getSize();
}
void Application::setBrushSize(float size) {
    if (m_currentToolMode == 2) m_selectionBrushTool.setSize(size);
    else if (m_currentToolMode == 3) m_penTool.setSize(size);
    else m_brushTool.setSize(size);

    m_shapeTool.setThickness(size);
}

float Application::getBrushSmoothing() const { return m_brushTool.getSmoothing(); }
void Application::setBrushSmoothing(float smoothing) { m_brushTool.setSmoothing(smoothing); }

float Application::getBrushJitter() const { return m_brushTool.getJitter(); }
void Application::setBrushJitter(float jitter) { m_brushTool.setJitter(jitter); }

float Application::getBrushFlow() const { return m_brushTool.getFlow(); }
void Application::setBrushFlow(float flow) { m_brushTool.setFlow(flow); }

float Application::getBrushSoftness() const { return m_brushTool.getSoftness(); }
void Application::setBrushSoftness(float softness) { m_brushTool.setSoftness(softness); }

bool Application::isEraser() const {
    if (m_currentToolMode == 2) return m_selectionBrushTool.isEraser();
    return m_brushTool.isEraser();
}
void Application::setEraser(bool isEraser) {
    if (m_currentToolMode == 2) m_selectionBrushTool.setEraser(isEraser);
    else m_brushTool.setEraser(isEraser);
}

sf::Vector2u Application::getWindowSize() const { return m_window.getSize(); }
Canvas& Application::getCanvas() const { return *m_canvas; }
AppState Application::getState() const { return m_state; }

void Application::setToolMode(int mode) {
    // Tell the old tool it is being put away
    if (m_activeTool) m_activeTool->onDeactivate(*m_canvas);

    m_currentToolMode = mode;
    if (mode == 0) m_activeTool = &m_brushTool;
    else if (mode == 1) m_activeTool = &m_rectSelectTool;
    else if (mode == 2) m_activeTool = &m_selectionBrushTool;
    else if (mode == 3) m_activeTool = &m_penTool;
    else if (mode == 4) m_activeTool = &m_freehandSelectTool;
    else if (mode == 5) m_activeTool = &m_paintBucketTool;
    else if (mode == 6) m_activeTool = &m_shapeTool;
    else if (mode == 7) m_activeTool = &m_cropTool;
    else if (mode == 8) m_activeTool = &m_transformTool;
    else if (mode == 9) m_activeTool = &m_colorPickerTool;

    // Tell the new tool it has been selected
    if (m_activeTool) m_activeTool->onActivate(*m_canvas);
}

int Application::getToolMode() const {
    return m_currentToolMode;
}

void Application::setShapeType(int type){
    m_shapeTool.setShapeType(type);
}

int Application::getShapeType() const {
    return m_shapeTool.getShapeType();
}

bool Application::isShapeFilled() const{
    return m_shapeTool.isFilled();
}

void Application::setShapeFilled(bool filled){
    m_shapeTool.setFilled(filled);
}

float Application::getWorkspaceRotation() const { 
    return m_workspaceRotation; 
}

void Application::setWorkspaceRotation(float rot) { 
    m_workspaceRotation = rot; 
}

float Application::getPenPressure() const { 
    return m_penPressure; 
}

void Application::setPenPressure(float p) { 
    // Intercept the hardware pressure and bend it along the curve
    m_penPressure = std::pow(p, m_pressureCurve);
}

void Application::loadProject(const std::string& filepath) {
    // Boot up the canvas and AI assistant
    // Dimensions don't matter, save file will overwrite them
    startDrawing(1920, 1080);

    // Tell the freshly booted canvas to load the file
    if (m_canvas->loadProject(filepath)) {
        m_currentFilePath = filepath;
        m_canvas->clearDirty();
    }
}

bool Application::saveCurrentProject() {
    if (m_currentFilePath.empty()) {
        return saveProjectAs();
    }
    else if (m_canvas) {
        bool success = m_canvas->saveProject(m_currentFilePath);
        // Clear the flag upon successful save
        if (success) m_canvas->clearDirty();
        return success;
    }
    return false;
}

bool Application::saveProjectAs() {
    std::string filepath = FileDialogs::saveFile("Drawing Project (*.drw)\0*.drw\0Any File\0*.*\0");
    if (!filepath.empty() && m_canvas) {
        m_currentFilePath = filepath;
        bool success = m_canvas->saveProject(m_currentFilePath);
        // Clear the flag upon successful save
        if (success) m_canvas->clearDirty();
        return success;
    }
    return false;
}

bool Application::isShortcutPressed(const std::string& action, const sf::Event::KeyPressed* key) {
    if (m_shortcuts.find(action) == m_shortcuts.end()) return false;
    Shortcut s = m_shortcuts[action];
    return (key->code == s.key && key->control == s.ctrl && key->shift == s.shift && key->alt == s.alt);
}

std::string Application::getShortcutString(const std::string& action) {
    if (m_shortcuts.find(action) == m_shortcuts.end()) return "";
    Shortcut s = m_shortcuts[action];
    if (s.key == sf::Keyboard::Key::Unknown) return "Unbound";

    std::string result = "";
    if (s.ctrl) result += "Ctrl + ";
    if (s.shift) result += "Shift + ";
    if (s.alt) result += "Alt + ";
    result += getKeyName(s.key);
    return result;
}

std::string Application::getKeyName(sf::Keyboard::Key key) {
    // Cast the enums to integers
    if (key >= sf::Keyboard::Key::A && key <= sf::Keyboard::Key::Z) {
        char c = 'A' + (static_cast<int>(key) - static_cast<int>(sf::Keyboard::Key::A));
        return std::string(1, c);
    }
    if (key >= sf::Keyboard::Key::Num0 && key <= sf::Keyboard::Key::Num9) {
        char c = '0' + (static_cast<int>(key) - static_cast<int>(sf::Keyboard::Key::Num0));
        return std::string(1, c);
    }

    switch (key) {
    case sf::Keyboard::Key::Escape: return "Esc";
    case sf::Keyboard::Key::Space: return "Space";
    case sf::Keyboard::Key::Enter: return "Enter";
    case sf::Keyboard::Key::LBracket: return "[";
    case sf::Keyboard::Key::RBracket: return "]";
    case sf::Keyboard::Key::Hyphen: return "-";
    case sf::Keyboard::Key::Equal: return "=";
    case sf::Keyboard::Key::LControl: return "LCtrl";
    case sf::Keyboard::Key::RControl: return "RCtrl";
    case sf::Keyboard::Key::LShift: return "LShift";
    case sf::Keyboard::Key::RShift: return "RShift";
    case sf::Keyboard::Key::LAlt: return "LAlt";
    case sf::Keyboard::Key::RAlt: return "RAlt";
    default: return "Key"; // Fallback for obscure keys
    }
}

void Application::saveSettings() {
    std::ofstream file("settings.ini");
    if (!file) return;

    file << "Theme=" << m_theme << "\n";
    file << "PressureCurve=" << m_pressureCurve << "\n";

    // Save every active shortcut in a pipe-separated format
    for (const auto& pair : m_shortcuts) {
        file << "Shortcut=" << pair.first << "|"
            << static_cast<int>(pair.second.key) << "|"
            << pair.second.ctrl << "|"
            << pair.second.shift << "|"
            << pair.second.alt << "\n";
    }
}

void Application::loadSettings() {
    std::ifstream file("settings.ini");
    if (!file) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        size_t delim = line.find('=');
        if (delim == std::string::npos) continue;

        std::string key = line.substr(0, delim);
        std::string val = line.substr(delim + 1);

        try {
            if (key == "Theme") {
                m_theme = std::stoi(val);
            }
            else if (key == "PressureCurve") {
                m_pressureCurve = std::stof(val);
            }
            else if (key == "Shortcut") {
                // Parse the format: Action Name|Key|Ctrl|Shift|Alt
                size_t p1 = val.find('|');
                size_t p2 = val.find('|', p1 + 1);
                size_t p3 = val.find('|', p2 + 1);
                size_t p4 = val.find('|', p3 + 1);

                if (p1 != std::string::npos && p2 != std::string::npos &&
                    p3 != std::string::npos && p4 != std::string::npos) {

                    std::string action = val.substr(0, p1);
                    int keyCode = std::stoi(val.substr(p1 + 1, p2 - p1 - 1));
                    bool ctrl = std::stoi(val.substr(p2 + 1, p3 - p2 - 1));
                    bool shift = std::stoi(val.substr(p3 + 1, p4 - p3 - 1));
                    bool alt = std::stoi(val.substr(p4 + 1));

                    m_shortcuts[action] = { static_cast<sf::Keyboard::Key>(keyCode), ctrl, shift, alt };
                }
            }
        }
        catch (...) {
            // If the text file was manually corrupted, safely ignore that specific line
            continue;
        }
    }
}