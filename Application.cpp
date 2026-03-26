#include "Application.h"

Application::Application()
    : m_window(
        sf::VideoMode::getDesktopMode(),
        "Licenta Desen C++",
        sf::Style::Default
    )
{
    m_window.setFramerateLimit(60);
    m_imgui.init(m_window);
    m_activeTool = std::make_unique<BrushTool>();
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
    m_state = AppState::DrawingEditor;
}

void Application::processEvents() {
    while (const std::optional event = m_window.pollEvent()) {
        m_imgui.processEvent(m_window, *event);

        if (event->is<sf::Event::Closed>()) {
            m_window.close();
            m_running = false;
        }

        if (m_state == AppState::DrawingEditor && m_canvas) {

            // Handle keyboard shortcuts for undo/redo
            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Z) {
                    m_canvas->undo();
                }
                else if (key->code == sf::Keyboard::Key::Y) {
                    m_canvas->redo();
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
}

void Application::render() {
    // 1. Clear the window to your dark gray UI background
    m_window.clear(sf::Color(40, 40, 40));

    if (m_state == AppState::DrawingEditor && m_canvas) {
        sf::Vector2f offset = getCanvasOffset();

        // 1. Draw the solid white "paper" background first
        sf::RectangleShape paperBg(sf::Vector2f(m_canvas->getLayers()[0]->texture->getSize()));
        paperBg.setPosition(offset);
        paperBg.setScale({ m_zoom, m_zoom });
        paperBg.setFillColor(sf::Color::White);
        m_window.draw(paperBg);

        // 2. Ask the Canvas to draw all its visible layers on top of the paper!
        m_canvas->renderToTarget(m_window, offset, m_zoom);
    }
    // 4. Draw UI
    m_imgui.render(m_window);
    m_window.display();
}

sf::Color Application::getBrushColor() const {
    return m_activeTool->getColor();
}

void Application::setBrushColor(const sf::Color& color) {
    m_activeTool->setColor(color);
}

float Application::getBrushSize() const {
    return m_activeTool->getSize();
}

void Application::setBrushSize(float size) {
    m_activeTool->setSize(size);
}

float Application::getBrushSmoothing() const {
    return m_activeTool->getSmoothing();
}

void Application::setBrushSmoothing(float smoothing) {
    m_activeTool->setSmoothing(smoothing);
}

float Application::getBrushJitter() const {
    return m_activeTool->getJitter();
}

void Application::setBrushJitter(float jitter) {
    m_activeTool->setJitter(jitter);
}

float Application::getBrushFlow() const {
    return m_activeTool->getFlow();
}

void Application::setBrushFlow(float flow) {
    m_activeTool->setFlow(flow);
}

float Application::getBrushSoftness() const {
    return m_activeTool->getSoftness();
}

void Application::setBrushSoftness(float softness) {
    m_activeTool->setSoftness(softness);
}

bool Application::isEraser() const {
    return m_activeTool->isEraser();
}

void Application::setEraser(bool isEraser) {
    m_activeTool->setEraser(isEraser);
}

sf::Vector2u Application::getWindowSize() const {
    return m_window.getSize();
}

Canvas& Application::getCanvas() const {
    return *m_canvas;
}

AppState Application::getState() const {
    return m_state;
}