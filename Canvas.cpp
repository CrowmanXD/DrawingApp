#include "Canvas.h"
#include "StrokeUndoCommand.h"
#include "LayerUndoCommands.h"
#include "ClipboardHelper.h"

Canvas::Canvas(sf::Vector2u size) : m_size(size) {
    m_compositeTexture = std::make_unique<sf::RenderTexture>(size);
    m_clippingTexture = std::make_unique<sf::RenderTexture>(size);
    // Automatically create the first layer
    addLayer();
    m_layers[0]->name = "Background";
    m_selectedLayers.insert(0);

    m_selectionTexture = std::make_unique<sf::RenderTexture>(size);
    m_selectionTexture->clear(sf::Color(0, 0, 0, 0));
    // Compile the real-time brush clipping shader
    const std::string fragShader = R"(
        uniform sampler2D baseTexture;
        uniform sampler2D selectionMask;
        uniform vec2 canvasSize;

        void main() {
            vec4 pixel = gl_Color * texture2D(baseTexture, gl_TexCoord[0].xy);
            vec2 maskUv = gl_FragCoord.xy / canvasSize;
            vec4 mask = texture2D(selectionMask, maskUv);
            
            gl_FragColor = vec4(pixel.rgb, pixel.a * mask.a);
        }
    )";

    if (sf::Shader::isAvailable()) {
        // Only attempt to set uniforms IF the shader successfully compiled
        // This permanently prevents the SFML 3 vector out-of-range crash.
        if (m_selectionShader.loadFromMemory(fragShader, sf::Shader::Type::Fragment)) {
            m_selectionShader.setUniform("canvasSize", sf::Vector2f(static_cast<float>(size.x), static_cast<float>(size.y)));
        }
    }
}

Canvas::~Canvas() = default;

sf::BlendMode Canvas::getSfmlBlendMode(LayerBlendMode mode, bool isClipped) const {
    if (isClipped) {
        return sf::BlendMode(sf::BlendMode::Factor::DstAlpha, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add);
    }
    if (mode == LayerBlendMode::Multiply) return sf::BlendMode(sf::BlendMode::Factor::DstColor, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add);
    if (mode == LayerBlendMode::Add) return sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add);
    return sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add, sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add);
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

    setActiveLayer(m_activeLayerIndex);

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
        // The active layer shifted up to fill the gap
        m_activeLayerIndex -= countToDelete;
    }

    setActiveLayer(m_activeLayerIndex);

    pushUndoCommand(std::make_unique<DeleteLayerCommand>(startIndex, std::move(removedLayers), oldActive));
}

void Canvas::mergeDown(int index) {
    if (index <= 0 || index >= m_layers.size()) return;

    int targetIndex = index - 1;
    auto& targetLayer = m_layers[targetIndex];
    auto& sourceLayer = m_layers[index];

    if (targetLayer->type != LayerType::Content || sourceLayer->type != LayerType::Content) return;

    sf::RenderTexture mergedTex(m_size);
    mergedTex.clear(sf::Color(0, 0, 0, 0));

    sf::Sprite targetSprite(targetLayer->texture->getTexture());
    targetSprite.setPosition(targetLayer->offset);
    targetSprite.setScale(targetLayer->scale);
    mergedTex.draw(targetSprite, sf::RenderStates(sf::BlendNone));

    if (sourceLayer->visible) {
        sf::Sprite sourceSprite(sourceLayer->texture->getTexture());
        sourceSprite.setPosition(sourceLayer->offset);
        sourceSprite.setScale(sourceLayer->scale);

        std::uint8_t alpha = static_cast<std::uint8_t>(sourceLayer->opacity * 255.0f);
        sourceSprite.setColor(sf::Color(alpha, alpha, alpha, alpha));
        mergedTex.draw(sourceSprite, sf::RenderStates(getSfmlBlendMode(sourceLayer->blendMode, sourceLayer->isClipped)));
    }
    mergedTex.display();

    auto mergedLayer = targetLayer->cloneMeta();
    mergedLayer->offset = { 0.f, 0.f }; // Reset transform because it's already baked into the pixels
    mergedLayer->scale = { 1.f, 1.f };
    mergedLayer->texture->clear(sf::Color(0, 0, 0, 0));
    mergedLayer->texture->draw(sf::Sprite(mergedTex.getTexture()), sf::RenderStates(sf::BlendNone));
    mergedLayer->texture->display();

    auto layerForCanvas = mergedLayer->cloneMeta();
    layerForCanvas->texture->clear(sf::Color(0, 0, 0, 0));
    layerForCanvas->texture->draw(sf::Sprite(mergedTex.getTexture()), sf::RenderStates(sf::BlendNone));
    layerForCanvas->texture->display();

    std::vector<std::unique_ptr<Layer>> extracted;
    extracted.push_back(std::move(m_layers[targetIndex]));
    extracted.push_back(std::move(m_layers[index]));

    m_layers.erase(m_layers.begin() + targetIndex, m_layers.begin() + index + 1);
    m_layers.insert(m_layers.begin() + targetIndex, std::move(layerForCanvas));
    m_activeLayerIndex = targetIndex;

    setActiveLayer(m_activeLayerIndex);

    pushUndoCommand(std::make_unique<MergeLayerCommand>(targetIndex, 2, std::move(extracted), std::move(mergedLayer)));
}

void Canvas::mergeFolder(int index) {
    if (index < 0 || index >= m_layers.size() || m_layers[index]->type != LayerType::Folder) return;

    int originalDepth = m_layers[index]->depth;
    int endIndex = index;
    while (endIndex + 1 < m_layers.size() && m_layers[endIndex + 1]->depth > originalDepth) endIndex++;

    int count = endIndex - index + 1;
    if (count == 1) return;

    std::vector<bool> visibilities(m_layers.size());
    for (int i = 0; i < m_layers.size(); ++i) {
        visibilities[i] = m_layers[i]->visible;
        m_layers[i]->visible = false;
    }

    for (int i = index; i <= endIndex; ++i) {
        m_layers[i]->visible = visibilities[i];
        m_layers[i]->depth -= originalDepth;
    }

    LayerBlendMode oldBlend = m_layers[index]->blendMode;
    float oldOp = m_layers[index]->opacity;
    m_layers[index]->blendMode = LayerBlendMode::Normal;
    m_layers[index]->opacity = 1.0f;

    renderComposite();

    auto mergedLayer = m_layers[index]->cloneMeta();
    mergedLayer->type = LayerType::Content; // Tell the engine this is now a flattened image
    mergedLayer->offset = { 0.f, 0.f };       // Reset transform because it's already baked into the pixels
    mergedLayer->scale = { 1.f, 1.f };
    mergedLayer->name = m_layers[index]->name + " (Merged)";
    mergedLayer->opacity = oldOp;
    mergedLayer->blendMode = (oldBlend == LayerBlendMode::PassThrough) ? LayerBlendMode::Normal : oldBlend;
    mergedLayer->texture->clear(sf::Color(0, 0, 0, 0));
    mergedLayer->texture->draw(sf::Sprite(m_compositeTexture->getTexture()), sf::RenderStates(sf::BlendNone));
    mergedLayer->texture->display();

    m_layers[index]->blendMode = oldBlend;
    m_layers[index]->opacity = oldOp;
    for (int i = 0; i < m_layers.size(); ++i) m_layers[i]->visible = visibilities[i];
    for (int i = index; i <= endIndex; ++i) m_layers[i]->depth += originalDepth;

    std::vector<std::unique_ptr<Layer>> extracted;
    for (int i = 0; i < count; ++i) {
        extracted.push_back(std::move(m_layers[index]));
        m_layers.erase(m_layers.begin() + index);
    }

    auto layerForCanvas = mergedLayer->cloneMeta();
    layerForCanvas->texture->clear(sf::Color(0, 0, 0, 0));
    layerForCanvas->texture->draw(sf::Sprite(m_compositeTexture->getTexture()), sf::RenderStates(sf::BlendNone));
    layerForCanvas->texture->display();

    m_layers.insert(m_layers.begin() + index, std::move(layerForCanvas));
    m_activeLayerIndex = index;

    setActiveLayer(m_activeLayerIndex);

    renderComposite();

    pushUndoCommand(std::make_unique<MergeLayerCommand>(index, count, std::move(extracted), std::move(mergedLayer)));
}

void Canvas::setActiveLayer(int index) {
    if (index >= 0 && index < m_layers.size()) {
        m_activeLayerIndex = index;
        m_selectedLayers.clear();
        m_selectedLayers.insert(index);
    }
}

void Canvas::toggleLayerSelection(int index, bool multiSelect) {
    if (!multiSelect) {
        m_selectedLayers.clear();
        m_selectedLayers.insert(index);
        m_activeLayerIndex = index;
    }
    else {
        if (m_selectedLayers.count(index)) {
            m_selectedLayers.erase(index);
            // Ensure we always have an active anchor point
            if (m_activeLayerIndex == index && !m_selectedLayers.empty()) {
                m_activeLayerIndex = *m_selectedLayers.begin();
            }
            else if (m_selectedLayers.empty()) {
                m_selectedLayers.insert(index); // Prevent de-selecting the very last layer
            }
        }
        else {
            m_selectedLayers.insert(index);
            m_activeLayerIndex = index;
        }
    }
}

bool Canvas::isLayerSelected(int index) const {
    return m_selectedLayers.count(index) > 0;
}

void Canvas::beginBatchCommand() {
    m_activeBatch = std::make_unique<BatchCommand>();
}

void Canvas::endBatchCommand() {
    if (m_activeBatch && !m_activeBatch->isEmpty()) {
        m_undoStack.push(std::move(m_activeBatch));
    }
    m_activeBatch.reset();
}

void Canvas::pushUndoCommand(std::unique_ptr<UndoCommand> cmd) {
    if (m_activeBatch) {
        m_activeBatch->addCommand(std::move(cmd));
    }
    else {
        m_undoStack.push(std::move(cmd));
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

    sf::RenderStates states(sf::BlendAlpha);

    // Automatically clip the brush stroke to the selection mask
    if (m_hasSelection && sf::Shader::isAvailable()) {
        try {
            m_selectionShader.setUniform("baseTexture", sf::Shader::CurrentTexture);
            m_selectionShader.setUniform("selectionMask", m_selectionTexture->getTexture());
            states.shader = &m_selectionShader;
        }
        catch (...) {}
    }

    getActiveTexture().draw(drawable, states);
    getActiveTexture().display();
}

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f position, const sf::RenderStates& states) {
    if (!m_inStroke) return;

    sf::RenderStates finalStates = states;

    // Automatically clip the brush stroke to the selection mask
    if (m_hasSelection && sf::Shader::isAvailable()) {
        try {
            m_selectionShader.setUniform("baseTexture", sf::Shader::CurrentTexture);
            m_selectionShader.setUniform("selectionMask", m_selectionTexture->getTexture());
            finalStates.shader = &m_selectionShader;
        }
        catch (...) {}
    }

    getActiveTexture().draw(drawable, finalStates);
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

    // HELPER 1: Flush the clipping mask FBO to the main target
    auto flushClippingBase = [&]() {
        if (activeClippingBase != nullptr) {
            m_clippingTexture->display();
            float finalOp = activeClippingBase->opacity * stack.back().opacityMultiplier;
            if (activeClippingBase->visible && stack.back().isVisible) {
                drawSprite(m_clippingTexture->getTexture(), stack.back().target, finalOp, getSfmlBlendMode(activeClippingBase->blendMode), stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
            activeClippingBase = nullptr;
        }
        };

    // HELPER 2: Render a standard layer using the stack math
    auto drawLayerNode = [&](Layer* layerToDraw, float opacityMult, bool isVis, sf::Vector2f accOffset, sf::Vector2f accScale) {
        float finalOp = layerToDraw->opacity * opacityMult;
        sf::Vector2f finalOffset = accOffset + sf::Vector2f(layerToDraw->offset.x * accScale.x, layerToDraw->offset.y * accScale.y);
        sf::Vector2f finalScale = { layerToDraw->scale.x * accScale.x, layerToDraw->scale.y * accScale.y };
        if (layerToDraw->visible && isVis) {
            drawSprite(layerToDraw->texture->getTexture(), stack.back().target, finalOp, getSfmlBlendMode(layerToDraw->blendMode), finalOffset, finalScale);
        }
        };

    for (int i = 0; i < m_layers.size(); ++i) {
        const auto& layer = m_layers[i];

        if (activeClippingBase != nullptr && !layer->isClipped) flushClippingBase();

        while (stack.size() > layer->depth + 1) {
            flushClippingBase();
            auto topNode = stack.back();
            stack.pop_back();
            if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
                topNode.layer->texture->display();
                drawLayerNode(topNode.layer, stack.back().opacityMultiplier, stack.back().isVisible, stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
        }

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
                m_clippingTexture->clear(sf::Color(0, 0, 0, 0));
                activeClippingBase = layer.get();
                if (layer->visible) drawSprite(layer->texture->getTexture(), m_clippingTexture.get(), 1.0f, getSfmlBlendMode(LayerBlendMode::Normal), layer->offset, layer->scale);
            }
            else if (layer->isClipped && activeClippingBase != nullptr) {
                if (layer->visible) drawSprite(layer->texture->getTexture(), m_clippingTexture.get(), layer->opacity, getSfmlBlendMode(LayerBlendMode::Normal, true), layer->offset, layer->scale);
            }
            else {
                drawLayerNode(layer.get(), stack.back().opacityMultiplier, stack.back().isVisible, stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
        }
    }

    flushClippingBase();

    while (stack.size() > 1) {
        auto topNode = stack.back();
        stack.pop_back();
        if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
            topNode.layer->texture->display();
            drawLayerNode(topNode.layer, stack.back().opacityMultiplier, stack.back().isVisible, stack.back().accumulatedOffset, stack.back().accumulatedScale);
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

void Canvas::copyToClipboard() {
    sf::RenderTexture tempTex(m_size);
    tempTex.clear(sf::Color(0, 0, 0, 0));

    sf::RenderStates states(sf::BlendNone);

    // If there is an active selection, mask the layer
    if (m_hasSelection && sf::Shader::isAvailable()) {
        try {
            m_selectionShader.setUniform("baseTexture", getActiveTexture().getTexture());
            m_selectionShader.setUniform("selectionMask", m_selectionTexture->getTexture());
            states.shader = &m_selectionShader;
        }
        catch (...) {}
    }

    // Draw the current active layer into the temporary texture
    tempTex.draw(sf::Sprite(getActiveTexture().getTexture()), states);
    tempTex.display();

    // Send it straight to the OS
    ClipboardHelper::setImage(tempTex.getTexture().copyToImage());
}

void Canvas::cutToClipboard() {
    // A Cut is just a Copy followed instantly by an Erase
    copyToClipboard();

    if (m_hasSelection) {
        clearSelectionOnSelectedLayers();
    }
    else {
        // If nothing is selected, clear the entire layer.
        beginBatchCommand();
        for (int sel : m_selectedLayers) {
            if (m_layers[sel]->type == LayerType::Content) {
                auto beforeImage = std::make_unique<sf::Image>(m_layers[sel]->texture->getTexture().copyToImage());
                m_layers[sel]->texture->clear(sf::Color(0, 0, 0, 0));
                m_layers[sel]->texture->display();
                auto afterImage = std::make_unique<sf::Image>(m_layers[sel]->texture->getTexture().copyToImage());
                pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(beforeImage), std::move(afterImage), sel));
            }
        }
        endBatchCommand();
        renderComposite();
    }
}

void Canvas::clearSelectionOnSelectedLayers() {
    if (!m_hasSelection) return;

    beginBatchCommand();

    // Mathematically subtracts destination alpha wherever the mask alpha is 1
    sf::BlendMode eraseMode(
        sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
        sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
    );

    sf::Sprite maskSprite(m_selectionTexture->getTexture());

    for (int sel : m_selectedLayers) {
        if (sel >= 0 && sel < m_layers.size() && m_layers[sel]->type == LayerType::Content) {
            auto beforeImage = std::make_unique<sf::Image>(m_layers[sel]->texture->getTexture().copyToImage());

            m_layers[sel]->texture->draw(maskSprite, sf::RenderStates(eraseMode));
            m_layers[sel]->texture->display();

            auto afterImage = std::make_unique<sf::Image>(m_layers[sel]->texture->getTexture().copyToImage());
            pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(beforeImage), std::move(afterImage), sel));
        }
    }

    endBatchCommand();
    renderComposite(); // Force a fresh composite so the screen updates instantly
}

void Canvas::importFromImage(const sf::Image& image, const std::string& layerName) {
    if (image.getSize().x == 0 || image.getSize().y == 0) return;

    sf::Texture loadedTex;
    if (!loadedTex.loadFromImage(image)) return;

    addLayer();
    auto& activeLayer = m_layers[m_activeLayerIndex];
    activeLayer->name = layerName;

    activeLayer->texture->clear(sf::Color(0, 0, 0, 0));
    activeLayer->texture->draw(sf::Sprite(loadedTex), sf::RenderStates(sf::BlendNone));
    activeLayer->texture->display();
}

bool Canvas::saveToFile(const std::string& filename) {
    renderComposite();
    return m_compositeTexture->getTexture().copyToImage().saveToFile(filename);
}

bool Canvas::loadFromFile(const std::string& filename) {
    sf::Image loadedImg;
    if (!loadedImg.loadFromFile(filename)) return false;

    importFromImage(loadedImg, "Imported Image");
    return true;
}