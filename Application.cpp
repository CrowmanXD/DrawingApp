#include "Application.h"

Application::Application()
    : m_window(
        sf::VideoMode({ 1200u, 800u }),
        "Licenta Desen C++",
        sf::Style::Default
    ),
    m_canvas(m_window.getSize())
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

void Application::processEvents() {
    while (const std::optional event = m_window.pollEvent()) {
        m_imgui.processEvent(m_window, *event);

        if (event->is<sf::Event::Closed>()) {
            m_window.close();
            m_running = false;
        }

        // Handle keyboard shortcuts for undo/redo
        if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
            if (key->code == sf::Keyboard::Key::Z) {
                m_canvas.undo();
            }
            else if (key->code == sf::Keyboard::Key::Y) {
                m_canvas.redo();
            }
        }

        if (!m_imgui.wantsCaptureMouse()) {
            if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (mouse->button == sf::Mouse::Button::Left) {
                    m_canvas.beginStroke();
                    m_activeTool->onMouseDown(m_canvas, sf::Vector2f(mouse->position));
                }
            }

            if (const auto* mouse = event->getIf<sf::Event::MouseMoved>()) {
                m_activeTool->onMouseMove(m_canvas, sf::Vector2f(mouse->position)
                );
            }

            if (const auto* mouse = event->getIf<sf::Event::MouseButtonReleased>()) {
                if (mouse->button == sf::Mouse::Button::Left) {
                    m_activeTool->onMouseUp(m_canvas, sf::Vector2f(mouse->position)
                    );
                    m_canvas.endStroke();
                }
            }
        }
    }
}

void Application::update(sf::Time deltaTime) {
    m_imgui.update(m_window, deltaTime, *this);
}

void Application::render() {
    m_window.clear(sf::Color(40, 40, 40));
    sf::Sprite canvasSprite(m_canvas.getFinalTexture());

    m_window.draw(canvasSprite);

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
