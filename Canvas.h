#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <memory>
#include <cstdint>
#include "UndoStack.h"

enum class LayerBlendMode {
    Normal = 0,
    Multiply = 1,
    Add = 2,
    PassThrough = 3
};

enum class LayerType {
    Content,
    Folder
};

struct Layer {
    std::string name;
    bool visible = true;
    float opacity = 1.0f;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
    std::unique_ptr<sf::RenderTexture> texture;

    LayerType type = LayerType::Content;
    int depth = 0;

    Layer(sf::Vector2u size, std::string layerName, LayerType layerType = LayerType::Content)
        : name(std::move(layerName)), type(layerType) {
        texture = std::make_unique<sf::RenderTexture>(size);
        texture->clear(sf::Color(0, 0, 0, 0));
        texture->display();

        if (type == LayerType::Folder) {
            blendMode = LayerBlendMode::PassThrough;
        }
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
    void moveLayer(int fromIndex, int toIndex);
    void setActiveLayer(int index);
    int getActiveLayerIndex() const { return m_activeLayerIndex; }
    std::vector<std::unique_ptr<Layer>>& getLayers() { return m_layers; }
    std::unique_ptr<Layer> removeLayer(int index);
    void insertLayer(int index, std::unique_ptr<Layer> layer);
    void pushUndoCommand(std::unique_ptr<UndoCommand> cmd);
    void moveToFolder(int layerIndex, int folderIndex);
    void removeFromFolder(int layerIndex);
    void dropLayerToReorder(int sourceIndex, int targetIndex);

    sf::Vector2u getSize() const { return m_size; }
    sf::RenderTexture& getActiveTexture();

    void addFolder();
    Layer* getActiveLayer() const { return m_layers[m_activeLayerIndex].get(); }

    void renderToTarget(sf::RenderTarget& target, sf::Vector2f offset, float zoom);
    void clear(const sf::Color& color = sf::Color::White);



private:
    sf::Vector2u m_size;
    std::vector<std::unique_ptr<Layer>> m_layers;
    int m_activeLayerIndex = 0;

    UndoStack m_undoStack;

    bool m_inStroke = false;
    sf::Image m_strokeBackup;
};
