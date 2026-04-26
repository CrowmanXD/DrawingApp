#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <memory>
#include <cstdint>
#include <set>
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
    bool alphaLocked = false;
    bool isClipped = false;
    float opacity = 1.0f;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
    std::unique_ptr<sf::RenderTexture> texture;

    sf::Vector2f offset = { 0.f, 0.f };
    sf::Vector2f scale = { 1.f, 1.f };

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

    std::unique_ptr<Layer> cloneMeta() const {
        auto newLayer = std::make_unique<Layer>(texture->getSize(), name);
        newLayer->visible = visible;
        newLayer->opacity = opacity;
        newLayer->blendMode = blendMode;
        newLayer->type = type;
        newLayer->depth = depth;
        newLayer->alphaLocked = alphaLocked;
        newLayer->isClipped = isClipped;
        return newLayer;
    }
};

class BatchCommand;

class Canvas {
public:
    explicit Canvas(sf::Vector2u size);
    ~Canvas();

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
    const std::vector<std::unique_ptr<Layer>>& getLayers() const { return m_layers; }
    std::unique_ptr<Layer> removeLayer(int index);
    void insertLayer(int index, std::unique_ptr<Layer> layer);
    void pushUndoCommand(std::unique_ptr<UndoCommand> cmd);
    void moveToFolder(int layerIndex, int folderIndex);
    void removeFromFolder(int layerIndex);
    void dropLayerToReorder(int sourceIndex, int targetIndex);
    void deleteLayer(int index);
    void mergeDown(int index);
    void mergeFolder(int index);
    void toggleLayerSelection(int index, bool multiSelect);
    bool isLayerSelected(int index) const;
    const std::set<int>& getSelectedLayers() const { return m_selectedLayers; }
    void addFolder();
    Layer* getActiveLayer() const { return m_layers[m_activeLayerIndex].get(); }
    void setLayerName(int index, const std::string& newName);
    void setLayerOpacity(int index, float newOpacity);
    void setLayerVisibility(int index, bool isVisible);

    // --- SELECTION MASK METHODS ---
    void setSelectionActive(bool active) { m_hasSelection = active; }
    bool hasSelection() const { return m_hasSelection; }
    sf::RenderTexture& getSelectionTexture() { return *m_selectionTexture; }
    const sf::Texture& getSelectionTextureConst() const { return m_selectionTexture->getTexture(); }
    void setSelectionBounds(const sf::FloatRect& bounds) { m_selectionBounds = bounds; }
    sf::FloatRect getSelectionBounds() const { return m_selectionBounds; }

    void setSelectionLive(bool live) { m_isSelectionLive = live; }
    bool isSelectionLive() const { return m_isSelectionLive; }
    void clearSelectionOnSelectedLayers(); // Deletes masked pixels across all active layers

    void beginBatchCommand();
    void endBatchCommand();

    sf::Vector2u getSize() const { return m_size; }
    sf::RenderTexture& getActiveTexture();

    void renderComposite();
    const sf::Texture& getCompositeTexture() const { return m_compositeTexture->getTexture(); }
    void bakeLayerTransform(int index, std::unique_ptr<sf::Image> beforeImage);

    void clear(const sf::Color& color = sf::Color::White);

    void copyToClipboard();
    void cutToClipboard();

    void importFromImage(const sf::Image& image, const std::string& layerName = "Pasted Image");
    bool saveToFile(const std::string& filename);
    bool loadFromFile(const std::string& filename);
    bool saveProject(const std::string& filename);
    bool loadProject(const std::string& filename);

private:
    sf::Vector2u m_size;
    std::vector<std::unique_ptr<Layer>> m_layers;
    int m_activeLayerIndex = 0;
    std::unique_ptr<sf::RenderTexture> m_compositeTexture;
    std::unique_ptr<sf::RenderTexture> m_clippingTexture;

    std::set<int> m_selectedLayers; // Tracks all highlighted layers
    std::unique_ptr<BatchCommand> m_activeBatch; // Intercepts actions to group them

    UndoStack m_undoStack;

    std::unique_ptr<sf::RenderTexture> m_selectionTexture;
    sf::Shader m_selectionShader;
    bool m_hasSelection = false;
    sf::FloatRect m_selectionBounds;
    bool m_isSelectionLive = false;

    sf::BlendMode getSfmlBlendMode(LayerBlendMode mode, bool isClipped = false) const;

    bool m_inStroke = false;
    sf::Image m_strokeBackup;
};
