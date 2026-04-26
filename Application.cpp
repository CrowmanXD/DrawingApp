#include "Application.h"
#include "ClipboardHelper.h"
#include "MockAssistant.h"
#include "LLMAssistant.h"

Application::Application()
    : m_window(
        sf::VideoMode::getDesktopMode(),
        "Licenta Desen C++",
        sf::Style::Default
    )
{
    m_window.setFramerateLimit(60);
    m_imgui.init(m_window);
    m_activeTool = &m_brushTool;

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
    m_imgui.shutdown();
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
    if (!m_canvas) return { 300.f, 0.f };

    float workspaceWidth = static_cast<float>(m_window.getSize().x) - 300.f;
    float workspaceHeight = static_cast<float>(m_window.getSize().y);

    float scaledWidth = static_cast<float>(m_canvas->getSize().x) * m_zoom;
    float scaledHeight = static_cast<float>(m_canvas->getSize().y) * m_zoom;

    float offsetX = 300.f + (workspaceWidth - scaledWidth) / 2.f + m_pan.x;
    float offsetY = (workspaceHeight - scaledHeight) / 2.f + m_pan.y;

    return { offsetX, offsetY };
}

// This is called by the UI when you click "Create"
void Application::startDrawing(unsigned int width, unsigned int height) {
    m_canvas = std::make_unique<Canvas>(sf::Vector2u(width, height));

    // Clear the new canvas to transparent white to avoid dark halos
    m_canvas->clear(sf::Color(255, 255, 255, 0));

    m_assistant = std::make_unique<AssistantController>(*m_canvas);

    // This expects a local AI server running on port 1234.
    m_assistant->setBackend(std::make_unique<LLMAssistant>("localhost", 1234));
    m_state = AppState::DrawingEditor;
}

void Application::processEvents() {
    while (const std::optional event = m_window.pollEvent()) {
        m_imgui.processEvent(m_window, *event);

        if (event->is<sf::Event::Closed>()) {
            m_window.close();
            m_running = false;
        }

        // --- HANDLE WINDOW RESIZING ---
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            // Update the view to the new size so the canvas doesn't stretch
            sf::FloatRect visibleArea({ 0.f, 0.f }, { static_cast<float>(resized->size.x), static_cast<float>(resized->size.y) });
            m_window.setView(sf::View(visibleArea));
        }

        if (m_state == AppState::DrawingEditor && m_canvas) {

            // Handle keyboard shortcuts
            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->control && key->code == sf::Keyboard::Key::Z) {
                    m_canvas->undo();
                }
                else if (key->control && key->code == sf::Keyboard::Key::Y) {
                    m_canvas->redo();
                }
                // --- CTRL + V PASTE SHORTCUT ---
                else if (key->control && key->code == sf::Keyboard::Key::V) {
                    sf::Image clipboardImg = ClipboardHelper::getImage();
                    if (clipboardImg.getSize().x > 0) {
                        m_canvas->importFromImage(clipboardImg, "Pasted Layer");
                    }
                }
                // --- COPY AND CUT SHORTCUTS ---
                else if (key->control && key->code == sf::Keyboard::Key::C) {
                    m_canvas->copyToClipboard();
                }
                else if (key->control && key->code == sf::Keyboard::Key::X) {
                    m_canvas->cutToClipboard();
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

                // --- MOUSE DOWN ---
                if (const auto* mousePress = event->getIf<sf::Event::MouseButtonPressed>()) {
                    float canvasX = static_cast<float>(mousePress->position.x) - offset.x;
                    float canvasY = static_cast<float>(mousePress->position.y) - offset.y;
                    sf::Vector2f canvasPos(canvasX, canvasY);

                    if (mousePress->button == sf::Mouse::Button::Middle) {
                        m_isPanning = true;
                        m_lastMousePos = mousePress->position;
                    } 
                    else if (mousePress->button == sf::Mouse::Button::Left) {
                        // Divide by zoom to map screen pixels to canvas pixels
                        float canvasX = (static_cast<float>(mousePress->position.x) - offset.x) / m_zoom;
                        float canvasY = (static_cast<float>(mousePress->position.y) - offset.y) / m_zoom;

                        if (m_canvas->getActiveLayer()->type != LayerType::Folder &&
                            canvasX >= 0.f && canvasX <= canvasSize.x &&
                            canvasY >= 0.f && canvasY <= canvasSize.y) {

                            m_canvas->beginStroke();
                            m_activeTool->onMouseDown(*m_canvas, { canvasX, canvasY });
                        }
                    }
                }

                // --- MOUSE MOVE ---
                if (const auto* mouseMove = event->getIf<sf::Event::MouseMoved>()) {
                    // Handle Panning
                    if (m_isPanning) {
                        sf::Vector2i delta = mouseMove->position - m_lastMousePos;
                        m_pan.x += static_cast<float>(delta.x);
                        m_pan.y += static_cast<float>(delta.y);
                        m_lastMousePos = mouseMove->position;
                    }
                    // Handle Drawing
                    else {
                        float canvasX = (static_cast<float>(mouseMove->position.x) - offset.x) / m_zoom;
                        float canvasY = (static_cast<float>(mouseMove->position.y) - offset.y) / m_zoom;
                        m_activeTool->onMouseMove(*m_canvas, { canvasX, canvasY });
                    }
                }

                // 3. Handle Mouse Up
                if (const auto* mouseRelease = event->getIf<sf::Event::MouseButtonReleased>()) {
                    // Stop Panning
                    if (mouseRelease->button == sf::Mouse::Button::Middle) {
                        m_isPanning = false;
                    }
                    // Stop Drawing
                    else if (mouseRelease->button == sf::Mouse::Button::Left) {
                        float canvasX = (static_cast<float>(mouseRelease->position.x) - offset.x) / m_zoom;
                        float canvasY = (static_cast<float>(mouseRelease->position.y) - offset.y) / m_zoom;
                        m_activeTool->onMouseUp(*m_canvas, { canvasX, canvasY });
                        m_canvas->endStroke();
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
        m_assistant->processPendingActions(m_assistantPreviewOnly);
    }
}

void Application::render() {
    // 1. Clear the window to your dark gray UI background
    m_window.clear(sf::Color(40, 40, 40));

    if (m_state == AppState::DrawingEditor && m_canvas) {
        sf::Vector2f offset = getCanvasOffset();

        // 1. Draw the solid white "paper" background first
        sf::RectangleShape paperBg(sf::Vector2f(m_canvas->getSize()));
        paperBg.setPosition(offset);
        paperBg.setScale({ m_zoom, m_zoom }); // Scale the white paper
        paperBg.setFillColor(sf::Color::White);
        m_window.draw(paperBg);

        // 2. Ask the Canvas to build its internal Flat Mask
        m_canvas->renderComposite();

        // 3. Draw the composite exactly on top of the paper, scaled by your Camera Zoom
        sf::Sprite compSprite(m_canvas->getCompositeTexture());
        compSprite.setPosition(offset);
        compSprite.setScale({ m_zoom, m_zoom });
        m_window.draw(compSprite);

        // --- DRAW THE EDGE-DETECTED SELECTION MASK ---
        if (m_canvas->hasSelection() || m_canvas->isSelectionLive()) {
            if (sf::Shader::isAvailable()) {
                try {
                    m_antsShader.setUniform("selectionMask", m_canvas->getSelectionTextureConst());
                    m_antsShader.setUniform("textureSize", sf::Vector2f(static_cast<float>(m_canvas->getSize().x), static_cast<float>(m_canvas->getSize().y)));
                    m_antsShader.setUniform("time", m_antsClock.getElapsedTime().asSeconds());

                    // We only need to draw ONE sprite now. The shader handles all the borders!
                    sf::Sprite antsOverlay(m_canvas->getSelectionTextureConst());
                    antsOverlay.setPosition(offset);
                    antsOverlay.setScale(sf::Vector2f(m_zoom, m_zoom));

                    m_window.draw(antsOverlay, &m_antsShader);
                }
                catch (...) {}
            }
        }
    }
    // 4. Draw UI
    m_imgui.render(m_window);
    m_window.display();
}

sf::Color Application::getBrushColor() const { return m_brushTool.getColor(); }
void Application::setBrushColor(const sf::Color& color) { m_brushTool.setColor(color); }

float Application::getBrushSize() const {
    if (m_currentToolMode == 2) return m_selectionBrushTool.getSize();
    return m_brushTool.getSize();
}
void Application::setBrushSize(float size) {
    if (m_currentToolMode == 2) m_selectionBrushTool.setSize(size);
    else m_brushTool.setSize(size);
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
    m_currentToolMode = mode;
    if (mode == 0) m_activeTool = &m_brushTool;
    else if (mode == 1) m_activeTool = &m_rectSelectTool;
    else if (mode == 2) m_activeTool = &m_selectionBrushTool;
}

int Application::getToolMode() const {
    return m_currentToolMode;
}