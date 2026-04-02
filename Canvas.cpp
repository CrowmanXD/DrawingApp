#include "Canvas.h"
#include "StrokeUndoCommand.h"
#include "LayerUndoCommands.h"

Canvas::Canvas(sf::Vector2u size) : m_size(size) {
    m_compositeTexture = std::make_unique<sf::RenderTexture>(size);
    m_clippingTexture = std::make_unique<sf::RenderTexture>(size);
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

void Canvas::deleteLayer(int index) {
    if (index < 0 || index >= m_layers.size()) return;

    int startIndex = index;
    int endIndex = index;

    // If it's a folder, identify the exact range of all the children nested inside it
    if (m_layers[index]->type == LayerType::Folder) {
        int folderDepth = m_layers[index]->depth;
        while (endIndex + 1 < m_layers.size() && m_layers[endIndex + 1]->depth > folderDepth) {
            endIndex++;
        }
    }

    int countToDelete = endIndex - startIndex + 1;

    // Failsafe: Prevent the user from deleting the very last layer/folder on the canvas
    if (countToDelete == m_layers.size()) return;

    int oldActive = m_activeLayerIndex;
    std::vector<std::unique_ptr<Layer>> removedLayers;

    // Extract them from the main canvas vector
    for (int i = 0; i < countToDelete; ++i) {
        removedLayers.push_back(std::move(m_layers[startIndex]));
        m_layers.erase(m_layers.begin() + startIndex);
    }

    // Adjust the active layer selection perfectly
    if (m_activeLayerIndex >= startIndex && m_activeLayerIndex <= endIndex) {
        // Deleted the active layer. Jump to the closest valid layer
        m_activeLayerIndex = std::max(0, std::min(startIndex, static_cast<int>(m_layers.size()) - 1));
    }
    else if (m_activeLayerIndex > endIndex) {
        // The active layer shifted up to fill the gap!
        m_activeLayerIndex -= countToDelete;
    }

    pushUndoCommand(std::make_unique<DeleteLayerCommand>(startIndex, std::move(removedLayers), oldActive));
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

void Canvas::renderComposite() {
    m_compositeTexture->clear(sf::Color(0, 0, 0, 0));

    struct RenderNode {
        sf::RenderTarget* target;
        Layer* layer;
        float opacityMultiplier;
        bool isVisible;
        sf::Vector2f accumulatedOffset;
        sf::Vector2f accumulatedScale;
    };

    std::vector<RenderNode> stack;
    stack.push_back({ m_compositeTexture.get(), nullptr, 1.0f, true, {0.f, 0.f}, {1.f, 1.f} });

    auto getBlendMode = [](LayerBlendMode mode) {
        if (mode == LayerBlendMode::Multiply) return sf::BlendMode(sf::BlendMode::Factor::DstColor, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add);
        if (mode == LayerBlendMode::Add) return sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add);
        return sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add);
        };

    // The mathematical magic that allows clipping layers to only draw where the base layer exists!
    sf::BlendMode clipBlendMode(
        sf::BlendMode::Factor::DstAlpha, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
        sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add
    );

    auto drawSprite = [&](const sf::Texture& tex, sf::RenderTarget* destTarget, float opacity, sf::BlendMode blendMode, sf::Vector2f offset, sf::Vector2f scale) {
        if (opacity <= 0.0f) return;
        sf::Sprite sprite(tex);
        sprite.setPosition(offset);
        sprite.setScale(scale);
        std::uint8_t alpha = static_cast<std::uint8_t>(opacity * 255.0f);
        sprite.setColor(sf::Color(alpha, alpha, alpha, alpha));
        destTarget->draw(sprite, sf::RenderStates(blendMode));
        };

    Layer* activeClippingBase = nullptr;

    for (int i = 0; i < m_layers.size(); ++i) {
        const auto& layer = m_layers[i];

        // 1. If we are in a clipping group, but the next layer is NOT clipped, flush the clipping group!
        if (activeClippingBase != nullptr && !layer->isClipped) {
            m_clippingTexture->display();
            float finalOp = activeClippingBase->opacity * stack.back().opacityMultiplier;
            // The FBO represents the local folder space, so we only apply the accumulated folder offset!
            if (activeClippingBase->visible && stack.back().isVisible) {
                drawSprite(m_clippingTexture->getTexture(), stack.back().target, finalOp, getBlendMode(activeClippingBase->blendMode), stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
            activeClippingBase = nullptr;
        }

        // 2. Pop closed folders
        while (stack.size() > layer->depth + 1) {
            // Failsafe: Flush clipping group if it crosses a folder boundary
            if (activeClippingBase != nullptr) {
                m_clippingTexture->display();
                float finalOp = activeClippingBase->opacity * stack.back().opacityMultiplier;
                if (activeClippingBase->visible && stack.back().isVisible) {
                    drawSprite(m_clippingTexture->getTexture(), stack.back().target, finalOp, getBlendMode(activeClippingBase->blendMode), stack.back().accumulatedOffset, stack.back().accumulatedScale);
                }
                activeClippingBase = nullptr;
            }

            auto topNode = stack.back();
            stack.pop_back();
            if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
                topNode.layer->texture->display();
                float finalOp = topNode.layer->opacity * stack.back().opacityMultiplier;
                sf::Vector2f finalOffset = stack.back().accumulatedOffset + sf::Vector2f(topNode.layer->offset.x * stack.back().accumulatedScale.x, topNode.layer->offset.y * stack.back().accumulatedScale.y);
                sf::Vector2f finalScale = { topNode.layer->scale.x * stack.back().accumulatedScale.x, topNode.layer->scale.y * stack.back().accumulatedScale.y };
                if (topNode.layer->visible && stack.back().isVisible) {
                    drawSprite(topNode.layer->texture->getTexture(), stack.back().target, finalOp, getBlendMode(topNode.layer->blendMode), finalOffset, finalScale);
                }
            }
        }

        // 3. Process Current Layer
        bool isBaseLayer = (!layer->isClipped && i + 1 < m_layers.size() && m_layers[i + 1]->isClipped && m_layers[i + 1]->depth == layer->depth);

        if (layer->type == LayerType::Folder) {
            bool inheritedVis = stack.back().isVisible && layer->visible;
            if (layer->blendMode == LayerBlendMode::PassThrough) {
                float combinedOp = stack.back().opacityMultiplier * layer->opacity;
                sf::Vector2f combinedScale = { stack.back().accumulatedScale.x * layer->scale.x, stack.back().accumulatedScale.y * layer->scale.y };
                sf::Vector2f combinedOffset = stack.back().accumulatedOffset + sf::Vector2f(layer->offset.x * stack.back().accumulatedScale.x, layer->offset.y * stack.back().accumulatedScale.y);
                stack.push_back({ stack.back().target, layer.get(), combinedOp, inheritedVis, combinedOffset, combinedScale });
            }
            else {
                layer->texture->clear(sf::Color(0, 0, 0, 0));
                stack.push_back({ layer->texture.get(), layer.get(), 1.0f, inheritedVis, {0.f, 0.f}, {1.f, 1.f} });
            }
        }
        else {
            if (isBaseLayer) {
                // Intercept rendering! Draw the base layer to the scratchpad FBO
                m_clippingTexture->clear(sf::Color(0, 0, 0, 0));
                activeClippingBase = layer.get();
                if (layer->visible) {
                    drawSprite(layer->texture->getTexture(), m_clippingTexture.get(), 1.0f, getBlendMode(LayerBlendMode::Normal), layer->offset, layer->scale);
                }
            }
            else if (layer->isClipped && activeClippingBase != nullptr) {
                // Intercept rendering! Draw the clipped layer to the scratchpad FBO with the mask blend mode
                if (layer->visible) {
                    drawSprite(layer->texture->getTexture(), m_clippingTexture.get(), layer->opacity, clipBlendMode, layer->offset, layer->scale);
                }
            }
            else {
                // Normal Layer execution
                float finalOp = layer->opacity * stack.back().opacityMultiplier;
                sf::Vector2f finalOffset = stack.back().accumulatedOffset + sf::Vector2f(layer->offset.x * stack.back().accumulatedScale.x, layer->offset.y * stack.back().accumulatedScale.y);
                sf::Vector2f finalScale = { layer->scale.x * stack.back().accumulatedScale.x, layer->scale.y * stack.back().accumulatedScale.y };
                if (layer->visible && stack.back().isVisible) {
                    drawSprite(layer->texture->getTexture(), stack.back().target, finalOp, getBlendMode(layer->blendMode), finalOffset, finalScale);
                }
            }
        }
    }

    // 4. Flush the final clipping group if it was the last layer in the list
    if (activeClippingBase != nullptr) {
        m_clippingTexture->display();
        float finalOp = activeClippingBase->opacity * stack.back().opacityMultiplier;
        if (activeClippingBase->visible && stack.back().isVisible) {
            drawSprite(m_clippingTexture->getTexture(), stack.back().target, finalOp, getBlendMode(activeClippingBase->blendMode), stack.back().accumulatedOffset, stack.back().accumulatedScale);
        }
    }

    // 5. Flush remaining isolated folders
    while (stack.size() > 1) {
        auto topNode = stack.back();
        stack.pop_back();
        if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
            topNode.layer->texture->display();
            float finalOp = topNode.layer->opacity * stack.back().opacityMultiplier;
            sf::Vector2f finalOffset = stack.back().accumulatedOffset + sf::Vector2f(topNode.layer->offset.x * stack.back().accumulatedScale.x, topNode.layer->offset.y * stack.back().accumulatedScale.y);
            sf::Vector2f finalScale = { topNode.layer->scale.x * stack.back().accumulatedScale.x, topNode.layer->scale.y * stack.back().accumulatedScale.y };
            if (topNode.layer->visible && stack.back().isVisible) {
                drawSprite(topNode.layer->texture->getTexture(), stack.back().target, finalOp, getBlendMode(topNode.layer->blendMode), finalOffset, finalScale);
            }
        }
    }

    m_compositeTexture->display();
}

void Canvas::bakeLayerTransform(int index, std::unique_ptr<sf::Image> beforeImage) {
    if (index < 0 || index >= m_layers.size() || !beforeImage) return;
    auto& layer = m_layers[index];

    if (layer->offset == sf::Vector2f(0.f, 0.f) && layer->scale == sf::Vector2f(1.f, 1.f)) return;

    // Rasterize the transformation onto a temporary canvas
    sf::RenderTexture bakedTexture(m_size);
    bakedTexture.clear(sf::Color(0, 0, 0, 0));

    sf::Sprite sprite(layer->texture->getTexture());
    sprite.setPosition(layer->offset);
    sprite.setScale(layer->scale);

    // BlendNone forces the transformed pixels to purely overwrite the background
    bakedTexture.draw(sprite, sf::RenderStates(sf::BlendNone));
    bakedTexture.display();

    auto afterImage = std::make_unique<sf::Image>(bakedTexture.getTexture().copyToImage());

    // Erase the old layer and paste the baked pixels
    layer->texture->clear(sf::Color(0, 0, 0, 0));
    layer->texture->draw(sf::Sprite(bakedTexture.getTexture()), sf::RenderStates(sf::BlendNone));
    layer->texture->display();

    // Reset the transform so the brush is strictly normal again!
    layer->offset = { 0.f, 0.f };
    layer->scale = { 1.f, 1.f };

    // Because it modifies pixels, we can natively reuse StrokeUndoCommand!
    pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(beforeImage), std::move(afterImage), index));
}

void Canvas::clear(const sf::Color& color) {
    getActiveTexture().clear(color);
    getActiveTexture().display();
}

bool Canvas::saveToFile(const std::string& filename) {
    renderComposite();
    return m_compositeTexture->getTexture().copyToImage().saveToFile(filename);
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