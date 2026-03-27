#include "Canvas.h"
#include "StrokeUndoCommand.h"
#include "LayerUndoCommands.h"

Canvas::Canvas(sf::Vector2u size) : m_size(size) {
    // Automatically create the first layer
    addLayer();
    m_layers[0]->name = "Background";
}

void Canvas::addLayer() {
    std::string name = "Layer " + std::to_string(m_layers.size() + 1);
    auto newLayer = std::make_unique<Layer>(m_size, name, LayerType::Content);

    int insertPos = m_layers.size();
    if (m_activeLayerIndex >= 0 && m_activeLayerIndex < m_layers.size()) {
        newLayer->depth = m_layers[m_activeLayerIndex]->depth;
        if (m_layers[m_activeLayerIndex]->type == LayerType::Folder) {
            newLayer->depth++;
        }
        // Insert right on top of the active layer so it stays in the same folder!
        insertPos = m_activeLayerIndex + 1;
    }

    m_layers.insert(m_layers.begin() + insertPos, std::move(newLayer));
    m_activeLayerIndex = insertPos;

    if (m_layers.size() > 1) {
        m_undoStack.push(std::make_unique<AddLayerCommand>(m_activeLayerIndex));
    }
}

void Canvas::addFolder() {
    std::string name = "Group " + std::to_string(m_layers.size() + 1);
    auto newFolder = std::make_unique<Layer>(m_size, name, LayerType::Folder);

    int insertPos = m_layers.size();
    if (m_activeLayerIndex >= 0 && m_activeLayerIndex < m_layers.size()) {
        newFolder->depth = m_layers[m_activeLayerIndex]->depth;
        insertPos = m_activeLayerIndex + 1;
    }

    m_layers.insert(m_layers.begin() + insertPos, std::move(newFolder));
    m_activeLayerIndex = insertPos;

    m_undoStack.push(std::make_unique<AddLayerCommand>(m_activeLayerIndex));
}

std::unique_ptr<Layer> Canvas::removeLayer(int index) {
    if (index < 0 || index >= m_layers.size()) return nullptr;

    // Extract the layer and erase it from the vector
    auto layer = std::move(m_layers[index]);
    m_layers.erase(m_layers.begin() + index);

    // Safely bump the active layer selection down
    if (m_activeLayerIndex >= m_layers.size()) {
        m_activeLayerIndex = std::max(0, static_cast<int>(m_layers.size()) - 1);
    }

    return layer;
}

void Canvas::insertLayer(int index, std::unique_ptr<Layer> layer) {
    if (!layer) return;

    if (index >= m_layers.size()) {
        m_layers.push_back(std::move(layer));
        m_activeLayerIndex = m_layers.size() - 1;
    }
    else {
        m_layers.insert(m_layers.begin() + index, std::move(layer));
        m_activeLayerIndex = index;
    }
}

void Canvas::moveLayer(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_layers.size() ||
        toIndex < 0 || toIndex >= m_layers.size() ||
        fromIndex == toIndex) return;

    // Extract the layer and remove it from its old position
    auto layer = std::move(m_layers[fromIndex]);
    m_layers.erase(m_layers.begin() + fromIndex);

    // Insert it into the new position
    m_layers.insert(m_layers.begin() + toIndex, std::move(layer));

    // Update the active layer index so your selection follows the shuffles
    if (m_activeLayerIndex == fromIndex) {
        m_activeLayerIndex = toIndex;
    }
    else {
        if (fromIndex < m_activeLayerIndex && toIndex >= m_activeLayerIndex) {
            m_activeLayerIndex--;
        }
        else if (fromIndex > m_activeLayerIndex && toIndex <= m_activeLayerIndex) {
            m_activeLayerIndex++;
        }
    }
}

void Canvas::moveToFolder(int layerIndex, int folderIndex) {
    if (layerIndex == folderIndex || m_layers[layerIndex]->type == LayerType::Folder) return;

    int newDepth = m_layers[folderIndex]->depth + 1;

    // Calculate where to drop the layer so it lands exactly inside the target folder
    int insertPos = (layerIndex > folderIndex) ? folderIndex + 1 : folderIndex;
    int oldDepth = m_layers[layerIndex]->depth;

    pushUndoCommand(std::make_unique<ReparentLayerCommand>(layerIndex, insertPos, oldDepth, newDepth));

    moveLayer(layerIndex, insertPos);
    m_layers[insertPos]->depth = newDepth;
}

void Canvas::removeFromFolder(int layerIndex) {
    int currentDepth = m_layers[layerIndex]->depth;
    if (currentDepth == 0) return;

    // Scan backwards to find the exact folder this layer currently belongs to
    int folderIndex = -1;
    for (int i = layerIndex - 1; i >= 0; --i) {
        if (m_layers[i]->type == LayerType::Folder && m_layers[i]->depth == currentDepth - 1) {
            folderIndex = i;
            break;
        }
    }
    if (folderIndex == -1) return; // Failsafe

    // Scan forwards to find the end of the folder group so we can place it right on top
    int endOfFolder = folderIndex + 1;
    while (endOfFolder < m_layers.size() && m_layers[endOfFolder]->depth > m_layers[folderIndex]->depth) {
        endOfFolder++;
    }

    int insertPos = endOfFolder - 1;
    int oldDepth = currentDepth;
    int newDepth = m_layers[folderIndex]->depth;

    pushUndoCommand(std::make_unique<ReparentLayerCommand>(layerIndex, insertPos, oldDepth, newDepth));

    moveLayer(layerIndex, insertPos);
    m_layers[insertPos]->depth = newDepth;
}

void Canvas::dropLayerToReorder(int sourceIndex, int targetIndex) {
    if (sourceIndex == targetIndex) return;

    int oldDepth = m_layers[sourceIndex]->depth;
    int newDepth = m_layers[targetIndex]->depth;

    // Use the existing command to save history!
    pushUndoCommand(std::make_unique<ReparentLayerCommand>(sourceIndex, targetIndex, oldDepth, newDepth));

    moveLayer(sourceIndex, targetIndex);

    // Because moveLayer shifts the vector and places the layer EXACTLY at targetIndex,
    // we can safely apply the inherited depth directly to targetIndex.
    m_layers[targetIndex]->depth = newDepth;
}

void Canvas::pushUndoCommand(std::unique_ptr<UndoCommand> cmd) {
    m_undoStack.push(std::move(cmd));
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
            getActiveTexture().clear(sf::Color(0, 0, 0, 0));
            getActiveTexture().draw(sf::Sprite(texture), sf::RenderStates(sf::BlendNone));
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
            getActiveTexture().clear(sf::Color(0, 0, 0, 0));
            getActiveTexture().draw(sf::Sprite(texture), sf::RenderStates(sf::BlendNone));
            getActiveTexture().display();
        }
    }
    m_undoStack.redo(*this);
}

void Canvas::renderToTarget(sf::RenderTarget& target, sf::Vector2f offset, float zoom) {
    struct RenderNode {
        sf::RenderTarget* target;
        Layer* layer;
        float opacityMultiplier;
        bool isVisible; // --- NEW: Track inherited visibility down the stack ---
    };

    std::vector<RenderNode> stack;
    // The main window starts fully visible (true)
    stack.push_back({ &target, nullptr, 1.0f, true });

    // --- NEW: Added parentVisible parameter ---
    auto drawNode = [&](Layer* sourceLayer, sf::RenderTarget* destTarget, float parentOpacity, bool parentVisible) {
        float finalOpacity = sourceLayer->opacity * parentOpacity;

        // --- NEW: Abort drawing if the layer OR its parent folder is hidden! ---
        if (!sourceLayer->visible || !parentVisible || finalOpacity <= 0.0f) return;

        sf::Sprite sprite(sourceLayer->texture->getTexture());

        if (destTarget == &target) {
            sprite.setPosition(offset);
            sprite.setScale({ zoom, zoom });
        }

        std::uint8_t alpha = static_cast<std::uint8_t>(finalOpacity * 255.0f);
        sprite.setColor(sf::Color(alpha, alpha, alpha, alpha));

        sf::BlendMode sfmlBlendMode;
        if (sourceLayer->blendMode == LayerBlendMode::Multiply) {
            sfmlBlendMode = sf::BlendMode(sf::BlendMode::Factor::DstColor, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add);
        }
        else if (sourceLayer->blendMode == LayerBlendMode::Add) {
            sfmlBlendMode = sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add);
        }
        else {
            sfmlBlendMode = sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add);
        }
        destTarget->draw(sprite, sf::RenderStates(sfmlBlendMode));
        };

    for (const auto& layer : m_layers) {
        // Pop folders off the stack
        while (stack.size() > layer->depth + 1) {
            auto topNode = stack.back();
            stack.pop_back();
            if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
                topNode.layer->texture->display();
                // Pass the current target's inherited visibility
                drawNode(topNode.layer, stack.back().target, stack.back().opacityMultiplier, stack.back().isVisible);
            }
        }

        if (layer->type == LayerType::Folder) {
            // --- NEW: A folder is only visible if both IT and its PARENT are visible ---
            bool inheritedVisibility = stack.back().isVisible && layer->visible;

            if (layer->blendMode == LayerBlendMode::PassThrough) {
                float combinedOpacity = stack.back().opacityMultiplier * layer->opacity;
                // Push the new inherited visibility into the stack!
                stack.push_back({ stack.back().target, layer.get(), combinedOpacity, inheritedVisibility });
            }
            else {
                layer->texture->clear(sf::Color(0, 0, 0, 0));
                // Push the new inherited visibility into the stack!
                stack.push_back({ layer->texture.get(), layer.get(), 1.0f, inheritedVisibility });
            }
        }
        else {
            // Pass the current target's inherited visibility
            drawNode(layer.get(), stack.back().target, stack.back().opacityMultiplier, stack.back().isVisible);
        }
    }

    // Flush remaining folders
    while (stack.size() > 1) {
        auto topNode = stack.back();
        stack.pop_back();
        if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
            topNode.layer->texture->display();
            // Pass the current target's inherited visibility
            drawNode(topNode.layer, stack.back().target, stack.back().opacityMultiplier, stack.back().isVisible);
        }
    }
}

void Canvas::clear(const sf::Color& color) {
    getActiveTexture().clear(color);
    getActiveTexture().display();
}

bool Canvas::saveToFile(const std::string& filename) {
    // 1. Create a temporary texture the exact size of the canvas
    sf::RenderTexture exportTexture(m_size);
    
    // Clear it to transparent black so the empty areas of your canvas are actually transparent in the PNG!
    exportTexture.clear(sf::Color(0, 0, 0, 0));

    // 2. Render all the layers to this texture at 1.0x zoom and 0,0 offset
    renderToTarget(exportTexture, {0.f, 0.f}, 1.0f);
    exportTexture.display();

    // 3. Download the pixels from the GPU and save them to the hard drive
    return exportTexture.getTexture().copyToImage().saveToFile(filename);
}

bool Canvas::loadFromFile(const std::string& filename) {
    sf::Texture loadedTex;
    if (!loadedTex.loadFromFile(filename)) return false;

    // 1. Create a brand new layer to hold the imported image
    addLayer();
    
    // addLayer() automatically updates m_activeLayerIndex to the newly created layer
    auto& activeLayer = m_layers[m_activeLayerIndex];
    activeLayer->name = "Imported Image";

    // 2. Draw the loaded image exactly as it is onto the new layer
    activeLayer->texture->clear(sf::Color(0, 0, 0, 0));
    
    // We use sf::BlendNone so it perfectly copies the image's transparency without mixing!
    activeLayer->texture->draw(sf::Sprite(loadedTex), sf::RenderStates(sf::BlendNone));
    activeLayer->texture->display();

    return true;
}
