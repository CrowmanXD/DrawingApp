#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <memory>
#include "UndoStack.h"

struct Layer {
    std::string name;
    bool visible = true;
    std::unique_ptr<sf::RenderTexture> texture;

    Layer(sf::Vector2u size, std::string layerName) : name(std::move(layerName)) {
        texture = std::make_unique<sf::RenderTexture>(size);
        // Layers must start transparent!
        texture->clear(sf::Color(255, 255, 255, 0));
        texture->display();
    }
};

class Canvas {
public:
    explicit Canvas(sf::Vector2u size);

    void draw(const sf::Drawable& drawable, sf::Vector2f position);
    void draw(const sf::Drawable& drawable, sf::Vector2f position, const sf::RenderStates& states);

    void beginStroke();
    void endStroke();

    void undo();
    void redo();

    // Layer Management Methods
    void addLayer();
    void setActiveLayer(int index);
    int getActiveLayerIndex() const { return m_activeLayerIndex; }
    std::vector<std::unique_ptr<Layer>>& getLayers() { return m_layers; }

    sf::Vector2u getSize() const { return m_size; }
    sf::RenderTexture& getActiveTexture();

    void renderToTarget(sf::RenderTarget& target, sf::Vector2f offset);
    void clear(const sf::Color& color = sf::Color::White);

private:
    sf::Vector2u m_size;
    std::vector<std::unique_ptr<Layer>> m_layers;
    int m_activeLayerIndex = 0;

    UndoStack m_undoStack;

    bool m_inStroke = false;
    sf::Image m_strokeBackup;
};
