#include "Canvas.h"
#include "StrokeUndoCommand.h"

Canvas::Canvas(sf::Vector2u size) : m_size(size) {
    // Automatically create the first layer
    addLayer();
    m_layers[0]->name = "Background";
}

void Canvas::addLayer() {
    std::string name = "Layer " + std::to_string(m_layers.size() + 1);
    m_layers.push_back(std::make_unique<Layer>(m_size, name));
    m_activeLayerIndex = m_layers.size() - 1; // Auto-select the new layer
}

void Canvas::setActiveLayer(int index) {
    if (index >= 0 && index < m_layers.size()) {
        m_activeLayerIndex = index;
    }
}

sf::RenderTexture& Canvas::getActiveTexture() {
    return *m_layers[m_activeLayerIndex]->texture;
}

void Canvas::beginStroke() {
    m_inStroke = true;
    m_strokeBackup = getActiveTexture().getTexture().copyToImage();
}

void Canvas::endStroke() {
    if (!m_inStroke)
        return;

    m_inStroke = false;

    // Capture the canvas state after drawing
    auto beforeImage = std::make_unique<sf::Image>(m_strokeBackup);
    auto afterImage = std::make_unique<sf::Image>(getActiveTexture().getTexture().copyToImage());

    // Create and push undo command
    auto cmd = std::make_unique<StrokeUndoCommand>(
        std::move(beforeImage), std::move(afterImage), m_activeLayerIndex
    );
    m_undoStack.push(std::move(cmd));
}

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f position) {
    if (!m_inStroke) return;
    getActiveTexture().draw(drawable, sf::RenderStates(sf::BlendAlpha));
    getActiveTexture().display();
}

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f position, const sf::RenderStates& states) {
    if (!m_inStroke) return;
    getActiveTexture().draw(drawable, states);
    getActiveTexture().display();
}

void Canvas::undo() {
    if (m_inStroke) {
        m_inStroke = false;
        sf::Texture texture;
        if (texture.loadFromImage(m_strokeBackup)) {
            // Must clear to transparent!
            getActiveTexture().clear(sf::Color(255, 255, 255, 0));
            getActiveTexture().draw(sf::Sprite(texture));
            getActiveTexture().display();
        }
    }
    m_undoStack.undo(*this);
}

void Canvas::redo() {
    if (m_inStroke) {
        m_inStroke = false;
        sf::Texture texture;
        if (texture.loadFromImage(m_strokeBackup)) {
            // Must clear to transparent!
            getActiveTexture().clear(sf::Color(255, 255, 255, 0));
            getActiveTexture().draw(sf::Sprite(texture));
            getActiveTexture().display();
        }
    }
    m_undoStack.redo(*this);
}

void Canvas::renderToTarget(sf::RenderTarget& target, sf::Vector2f offset) {
    // Loop through all layers from bottom to top and draw them
    for (const auto& layer : m_layers) {
        if (layer->visible) {
            sf::Sprite sprite(layer->texture->getTexture());
            sprite.setPosition(offset);
            target.draw(sprite);
        }
    }
}

void Canvas::clear(const sf::Color& color) {
    getActiveTexture().clear(color);
    getActiveTexture().display();
}
