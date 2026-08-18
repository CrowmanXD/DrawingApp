#include "Canvas.h"
#include "StrokeUndoCommand.h"
#include "LayerUndoCommands.h"
#include "ClipboardHelper.h"
#include <fstream>
#include <cstring>

Canvas::Canvas(sf::Vector2u size) : m_size(size) {
    m_compositeTexture = std::make_unique<sf::RenderTexture>(size);
    m_clippingTexture = std::make_unique<sf::RenderTexture>(size);
    // Automatically create the first layer
    addLayer();
    m_layers[0]->name = "layer 1";
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
            
            // Multiply the RGB by the mask alpha to avoid ghost pixels
            gl_FragColor = vec4(pixel.rgb * mask.a, pixel.a * mask.a);
        }
    )";

    if (sf::Shader::isAvailable()) {
        // Only attempt to set uniforms if the shader successfully compiled
        if (m_selectionShader.loadFromMemory(fragShader, sf::Shader::Type::Fragment)) {
            m_selectionShader.setUniform("canvasSize", sf::Vector2f(static_cast<float>(size.x), static_cast<float>(size.y)));
        }
    }
}

Canvas::~Canvas() = default;

sf::BlendMode Canvas::getSfmlBlendMode(LayerBlendMode mode, bool isClipped) const {
    int modeInt = static_cast<int>(mode);

    if (isClipped) {
        // --- CLIPPED BLENDING (Must mask using Destination Alpha) ---
        if (modeInt == 1) { // Multiply
            return sf::BlendMode(
                sf::BlendMode::Factor::DstColor, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add
            );
        }
        else if (modeInt == 2) { // Add (Linear Dodge)
            return sf::BlendMode(
                sf::BlendMode::Factor::DstAlpha, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add
            );
        }
        else { // Normal
            return sf::BlendMode(
                sf::BlendMode::Factor::DstAlpha, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add
            );
        }
    }
    else {
        // --- STANDARD BLENDING ---
        if (modeInt == 1) { // Multiply
            return sf::BlendMode(
                sf::BlendMode::Factor::DstColor, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
            );
        }
        else if (modeInt == 2) { // Add (Linear Dodge)
            return sf::BlendMode(
                sf::BlendMode::Factor::One, sf::BlendMode::Factor::One, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
            );
        }
        else { // Normal (Premultiplied)
            return sf::BlendMode(
                sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
                sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
            );
        }
    }
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
        // Insert right on top of the active layer so it stays in the same folder
        insertPos = m_activeLayerIndex + 1;
    }

    m_layers.insert(m_layers.begin() + insertPos, std::move(newLayer));
    m_activeLayerIndex = insertPos;

    if (m_layers.size() > 1) {
        pushUndoCommand(std::make_unique<AddLayerCommand>(m_activeLayerIndex));
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

    pushUndoCommand(std::make_unique<AddLayerCommand>(m_activeLayerIndex));
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

    // Passing Transparent Black forces the folder to render with a transparent 
    // background so it doesn't bake white pixels into the final flattened image
    renderComposite(sf::Color(0, 0, 0, 0));

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

    // Restore standard screen rendering with the White background
    renderComposite(sf::Color::White);

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

void Canvas::setLayerName(int index, const std::string& newName) {
    if (index >= 0 && index < m_layers.size()) {
        m_layers[index]->name = newName;
        // You could trigger UI updates or layer re-renders here
    }
}

void Canvas::setLayerOpacity(int index, float newOpacity) {
    if (index >= 0 && index < m_layers.size()) {
        m_layers[index]->opacity = std::clamp(newOpacity, 0.0f, 1.0f);
    }
}

void Canvas::setLayerVisibility(int index, bool isVisible) {
    if (index >= 0 && index < m_layers.size()) {
        m_layers[index]->visible = isVisible;
    }
}

void Canvas::setLayerBlendMode(int index, int newMode) {
    if (index >= 0 && index < m_layers.size()) {
        // Cast the integer back to your custom enum
        m_layers[index]->blendMode = static_cast<LayerBlendMode>(newMode);
    }
}

void Canvas::applyCrop(const sf::IntRect& rect) {
    if (rect.size.x <= 0 || rect.size.y <= 0) return;

    // Shrink the canvas
    m_size = sf::Vector2u(rect.size.x, rect.size.y);

    // Recreate the global utility textures
    m_compositeTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_clippingTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_selectionTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_selectionTexture->clear(sf::Color(0, 0, 0, 0));

    // Shift and cut every layer
    for (auto& layer : m_layers) {
        sf::Image oldImg = layer->texture->getTexture().copyToImage();
        layer->texture = std::make_unique<sf::RenderTexture>(m_size);
        layer->texture->clear(sf::Color(0, 0, 0, 0));

        sf::Texture tempTex;
        if (tempTex.loadFromImage(oldImg)) {
            sf::Sprite sprite(tempTex);
            // Offset it backwards so the top-left of the crop box becomes (0,0)
            sprite.setPosition(sf::Vector2f(-rect.position.x, -rect.position.y));
            layer->texture->draw(sprite, sf::RenderStates(sf::BlendNone));
        }
        layer->texture->display();
    }
}

void Canvas::resizeQuietly(sf::Vector2u newSize) {
    m_size = newSize;
    m_compositeTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_clippingTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_selectionTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_selectionTexture->clear(sf::Color(0, 0, 0, 0));

    for (auto& layer : m_layers) {
        layer->texture = std::make_unique<sf::RenderTexture>(m_size);
        layer->texture->clear(sf::Color(0, 0, 0, 0));
    }
}

void Canvas::flipCanvasHorizontal() {
    beginBatchCommand();

    // Flip every visual layer
    for (int i = 0; i < m_layers.size(); ++i) {
        auto& layer = m_layers[i];

        if (layer->type == LayerType::Content && !layer->isLocked) {
            // Pre-bake active layer transformations to ensure pixel-perfect flips
            if (layer->offset != sf::Vector2f(0.f, 0.f) || layer->scale != sf::Vector2f(1.f, 1.f)) {
                auto preBakeImg = std::make_unique<sf::Image>(layer->texture->getTexture().copyToImage());
                bakeLayerTransform(i, std::move(preBakeImg));
            }

            auto beforeImage = std::make_unique<sf::Image>(layer->texture->getTexture().copyToImage());

            // GPU-Accelerated Flip
            sf::RenderTexture tempTex(m_size);
            tempTex.clear(sf::Color(0, 0, 0, 0));

            sf::Sprite sprite(layer->texture->getTexture());
            // Set origin to the mathematical center of the canvas
            sprite.setOrigin(sf::Vector2f(m_size.x / 2.f, m_size.y / 2.f));
            sprite.setPosition(sf::Vector2f(m_size.x / 2.f, m_size.y / 2.f));
            // A negative X scale mirrors the texture horizontally
            sprite.setScale(sf::Vector2f(-1.f, 1.f));

            tempTex.draw(sprite, sf::RenderStates(sf::BlendNone));
            tempTex.display();

            auto afterImage = std::make_unique<sf::Image>(tempTex.getTexture().copyToImage());

            // Save the flipped texture back to the layer
            layer->texture->clear(sf::Color(0, 0, 0, 0));
            layer->texture->draw(sf::Sprite(tempTex.getTexture()), sf::RenderStates(sf::BlendNone));
            layer->texture->display();

            // Store in the batch Undo history
            pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(beforeImage), std::move(afterImage), i));
        }
    }

    // Flip the active selection mask (if the Lasso/Rect select is active)
    if (m_hasSelection) {
        sf::RenderTexture tempMask(m_size);
        tempMask.clear(sf::Color(0, 0, 0, 0));

        sf::Sprite maskSprite(m_selectionTexture->getTexture());
        maskSprite.setOrigin(sf::Vector2f(m_size.x / 2.f, m_size.y / 2.f));
        maskSprite.setPosition(sf::Vector2f(m_size.x / 2.f, m_size.y / 2.f));
        maskSprite.setScale(sf::Vector2f(-1.f, 1.f));

        tempMask.draw(maskSprite, sf::RenderStates(sf::BlendNone));
        tempMask.display();

        m_selectionTexture->clear(sf::Color(0, 0, 0, 0));
        m_selectionTexture->draw(sf::Sprite(tempMask.getTexture()), sf::RenderStates(sf::BlendNone));
        m_selectionTexture->display();
    }

    endBatchCommand();
    renderComposite(); // Force the screen to update instantly
}

void Canvas::beginBatchCommand() {
    m_activeBatch = std::make_unique<BatchCommand>();
}

void Canvas::endBatchCommand() {
    // Manually trigger the flag for batch commands
    m_isDirty = true;
    if (m_activeBatch && !m_activeBatch->isEmpty()) {
        m_undoStack.push(std::move(m_activeBatch));
    }
    m_activeBatch.reset();
}

void Canvas::pushUndoCommand(std::unique_ptr<UndoCommand> cmd) {
    m_isDirty = true; // Mark as modified
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
    m_strokeModified = false;
    m_strokeBackup = getActiveTexture().getTexture().copyToImage();
}

void Canvas::restoreStrokeBackup() {
    if (!m_inStroke) return;

    sf::Texture texture;
    if (texture.loadFromImage(m_strokeBackup)) {
        getActiveTexture().clear(sf::Color(0, 0, 0, 0));
        getActiveTexture().draw(sf::Sprite(texture), sf::RenderStates(sf::BlendNone));
        // Don't call display() yet because the Tool is about to draw the preview on top
    }
}

void Canvas::endStroke() {
    if (!m_inStroke)
        return;

    m_inStroke = false;

    if (!m_strokeModified) return;

    // Tell the UI to update the thumbnail
    m_layers[m_activeLayerIndex]->boundsDirty = true;

    // Capture the canvas state after drawing
    auto beforeImage = std::make_unique<sf::Image>(m_strokeBackup);
    auto afterImage = std::make_unique<sf::Image>(getActiveTexture().getTexture().copyToImage());

    // Create and push undo command
    auto cmd = std::make_unique<StrokeUndoCommand>(
        std::move(beforeImage), std::move(afterImage), m_activeLayerIndex
    );
    pushUndoCommand(std::move(cmd));
}

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f position) {
    if (!m_inStroke) return;

    m_strokeModified = true; //Flag that pixels were actually modified

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

    m_strokeModified = true; //Flag that pixels were actually modified

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
    m_isDirty = true; // Mark as modified
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
    m_isDirty = true; // Mark as modified
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

void Canvas::renderComposite(sf::Color clearColor) {
    // Clear the final output texture with the specified background color (usually white or transparent)
    m_compositeTexture->clear(clearColor);

    // RenderNode acts as a frame in our iterative tree-traversal stack.
    // It keeps track of the inherited state (opacity, transforms) as we dive deeper into folders.
    struct RenderNode {
        sf::RenderTarget* target;          // The Framebuffer Object (FBO) we are currently drawing into
        Layer* layer;                      // The folder layer associated with this node (nullptr for the root canvas)
        float opacityMultiplier;           // Inherited opacity from parent folders
        bool isVisible;                    // Inherited visibility from parent folders
        sf::Vector2f accumulatedOffset;    // Inherited position shifts from parent folders
        sf::Vector2f accumulatedScale;     // Inherited scaling from parent folders
    };

    std::vector<RenderNode> stack;
    // Push the root canvas node. All top-level layers will draw directly to m_compositeTexture.
    stack.push_back({ m_compositeTexture.get(), nullptr, 1.0f, true, {0.f, 0.f}, {1.f, 1.f} });

    // --- UTILITY LAMBDAS ---

    // Generic helper to draw a texture to a specific target with applied transformations and blending
    auto drawSprite = [&](const sf::Texture& tex, sf::RenderTarget* destTarget, float opacity, sf::BlendMode blendMode, sf::Vector2f offset, sf::Vector2f scale) {
        if (opacity <= 0.0f) return; // Optimization: Skip rendering entirely transparent pixels

        sf::Sprite sprite(tex);
        sprite.setPosition(offset);
        sprite.setScale(scale);

        // Convert normalized opacity (0.0 - 1.0) to 8-bit alpha (0 - 255)
        std::uint8_t alpha = static_cast<std::uint8_t>(opacity * 255.0f);
        sprite.setColor(sf::Color(alpha, alpha, alpha, alpha));

        destTarget->draw(sprite, sf::RenderStates(blendMode));
        };

    // Tracks the base layer of a clipping mask group. If null, we are not currently processing a clipping group.
    Layer* activeClippingBase = nullptr;

    // HELPER 1: Flush the clipping mask FBO to the main target
    // Called when a clipping group ends. It takes the composite result of the clipping texture
    // and stamps it onto the current active render target (from the stack)
    auto flushClippingBase = [&]() {
        if (activeClippingBase != nullptr) {
            m_clippingTexture->display(); // Finalize the FBO

            float finalOp = activeClippingBase->opacity * stack.back().opacityMultiplier;
            if (activeClippingBase->visible && stack.back().isVisible) {
                drawSprite(m_clippingTexture->getTexture(), stack.back().target, finalOp,
                    getSfmlBlendMode(activeClippingBase->blendMode),
                    stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
            activeClippingBase = nullptr; // Reset the state machine
        }
        };

    // HELPER 2: Render a standard standalone layer applying the inherited stack math
    auto drawLayerNode = [&](Layer* layerToDraw, float opacityMult, bool isVis, sf::Vector2f accOffset, sf::Vector2f accScale) {
        float finalOp = layerToDraw->opacity * opacityMult;

        // Calculate the absolute world position and scale by multiplying with parent transforms
        sf::Vector2f finalOffset = accOffset + sf::Vector2f(layerToDraw->offset.x * accScale.x, layerToDraw->offset.y * accScale.y);
        sf::Vector2f finalScale = { layerToDraw->scale.x * accScale.x, layerToDraw->scale.y * accScale.y };

        if (layerToDraw->visible && isVis) {
            drawSprite(layerToDraw->texture->getTexture(), stack.back().target, finalOp,
                getSfmlBlendMode(layerToDraw->blendMode), finalOffset, finalScale);
        }
        };

    // --- MAIN RENDERING LOOP ---
    // Iterate through layers strictly from bottom to top (Back-to-Front Painter's Algorithm)
    for (int i = 0; i < m_layers.size(); ++i) {
        const auto& layer = m_layers[i];

        // State Machine: If we were building a clipping mask but the current layer is NOT clipped, 
        // the clipping group has ended. Must flush the buffer.
        if (activeClippingBase != nullptr && !layer->isClipped) {
            flushClippingBase();
        }

        // Handle Folder Hierarchy (Ascending the tree)
        // If the stack size is larger than the required depth, we have exited one or more folders
        while (stack.size() > layer->depth + 1) {
            flushClippingBase(); // Ensure any internal clipping masks are finalized first

            auto topNode = stack.back();
            stack.pop_back();

            // If the folder was NOT PassThrough (e.g., Normal blend mode), its children were drawn 
            // into its private texture. We must now stamp that baked texture onto the parent target
            if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
                topNode.layer->texture->display();
                drawLayerNode(topNode.layer, stack.back().opacityMultiplier, stack.back().isVisible,
                    stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
        }

        // Look-ahead to see if the current layer serves as the base for a clipping mask group
        bool isBaseLayer = (!layer->isClipped && i + 1 < m_layers.size() &&
            m_layers[i + 1]->isClipped && m_layers[i + 1]->depth == layer->depth);

        // Handle Folder Hierarchy (Descending into the tree)
        if (layer->type == LayerType::Folder) {
            bool inheritedVis = stack.back().isVisible && layer->visible;

            if (layer->blendMode == LayerBlendMode::PassThrough) {
                // PassThrough Mode: Children draw directly to the parent's target
                // The folder acts purely as a math container for opacity and transforms
                float combinedOp = stack.back().opacityMultiplier * layer->opacity;
                sf::Vector2f combinedScale = { stack.back().accumulatedScale.x * layer->scale.x, stack.back().accumulatedScale.y * layer->scale.y };
                sf::Vector2f combinedOffset = stack.back().accumulatedOffset + sf::Vector2f(layer->offset.x * stack.back().accumulatedScale.x, layer->offset.y * stack.back().accumulatedScale.y);

                stack.push_back({ stack.back().target, layer.get(), combinedOp, inheritedVis, combinedOffset, combinedScale });
            }
            else {
                // Isolated Mode (Normal): Children draw into this folder's private FBO
                // The folder isolates blending from the rest of the canvas until it is popped
                layer->texture->clear(sf::Color(0, 0, 0, 0));
                stack.push_back({ layer->texture.get(), layer.get(), 1.0f, inheritedVis, {0.f, 0.f}, {1.f, 1.f} });
            }
        }
        // Handle Standard Content Layers
        else {
            if (isBaseLayer) {
                // Initialize the clipping mask FBO and draw the base shape into it
                m_clippingTexture->clear(sf::Color(0, 0, 0, 0));
                activeClippingBase = layer.get();
                if (layer->visible) {
                    drawSprite(layer->texture->getTexture(), m_clippingTexture.get(), 1.0f,
                        getSfmlBlendMode(LayerBlendMode::Normal), layer->offset, layer->scale);
                }
            }
            else if (layer->isClipped && activeClippingBase != nullptr) {
                // Draw clipped contents OVER the base shape using Destination-Alpha blending
                if (layer->visible) {
                    drawSprite(layer->texture->getTexture(), m_clippingTexture.get(), layer->opacity,
                        getSfmlBlendMode(layer->blendMode, true), layer->offset, layer->scale);
                }
            }
            else {
                // Standard unclipped layer rendering
                drawLayerNode(layer.get(), stack.back().opacityMultiplier, stack.back().isVisible,
                    stack.back().accumulatedOffset, stack.back().accumulatedScale);
            }
        }
    }

    // --- POST-LOOP CLEANUP ---

    // Flush any pending clipping group that reached the very end of the layer stack
    flushClippingBase();

    // Collapse any remaining open folders in the stack back down to the root canvas
    while (stack.size() > 1) {
        auto topNode = stack.back();
        stack.pop_back();

        if (topNode.layer && topNode.layer->blendMode != LayerBlendMode::PassThrough) {
            topNode.layer->texture->display();
            drawLayerNode(topNode.layer, stack.back().opacityMultiplier, stack.back().isVisible,
                stack.back().accumulatedOffset, stack.back().accumulatedScale);
        }
    }

    // Finalize the master composite texture so it is ready to be drawn to the OS Window
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
        if (sel >= 0 && sel < m_layers.size() && m_layers[sel]->type == LayerType::Content && !m_layers[sel]->isLocked) {
            // Tell the UI to update the thumbnail
            m_layers[sel]->boundsDirty = true;

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

void Canvas::copySelectionToNewLayer() {
    if (!m_hasSelection) return;

    // Find the source layer (we pull from the top-most selected drawing layer)
    int srcIdx = -1;
    for (int sel : m_selectedLayers) {
        if (m_layers[sel]->type == LayerType::Content && !m_layers[sel]->isLocked) {
            srcIdx = sel;
            break;
        }
    }
    if (srcIdx == -1) return;

    // Mathematically mask the active layer with the selection texture and extract the pixels
    sf::RenderTexture tempTex(m_size);
    tempTex.clear(sf::Color(0, 0, 0, 0));
    sf::RenderStates states(sf::BlendNone);
    if (sf::Shader::isAvailable()) {
        m_selectionShader.setUniform("baseTexture", m_layers[srcIdx]->texture->getTexture());
        m_selectionShader.setUniform("selectionMask", m_selectionTexture->getTexture());
        states.shader = &m_selectionShader;
    }
    tempTex.draw(sf::Sprite(m_layers[srcIdx]->texture->getTexture()), states);
    tempTex.display();

    sf::Image copiedPixels = tempTex.getTexture().copyToImage();

    // Batch the layer creation and pixel stamping so Undo removes the layer cleanly
    beginBatchCommand();
    addLayer();
    m_layers[m_activeLayerIndex]->name = "Copied Layer";

    auto beforeImg = std::make_unique<sf::Image>(m_layers[m_activeLayerIndex]->texture->getTexture().copyToImage());

    sf::Texture pasteTex;
    pasteTex.loadFromImage(copiedPixels);
    m_layers[m_activeLayerIndex]->texture->draw(sf::Sprite(pasteTex), sf::RenderStates(sf::BlendNone));
    m_layers[m_activeLayerIndex]->texture->display();

    auto afterImg = std::make_unique<sf::Image>(m_layers[m_activeLayerIndex]->texture->getTexture().copyToImage());
    pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(beforeImg), std::move(afterImg), m_activeLayerIndex));

    endBatchCommand();
    renderComposite();

    // Clear the selection mask
    getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
    setSelectionActive(false);
}

void Canvas::cutSelectionToNewLayer() {
    if (!m_hasSelection) return;

    int srcIdx = -1;
    for (int sel : m_selectedLayers) {
        if (m_layers[sel]->type == LayerType::Content && !m_layers[sel]->isLocked) {
            srcIdx = sel;
            break;
        }
    }
    if (srcIdx == -1) return;

    beginBatchCommand();

    // Extract the masked pixels first
    sf::RenderTexture tempTex(m_size);
    tempTex.clear(sf::Color(0, 0, 0, 0));
    sf::RenderStates states(sf::BlendNone);
    if (sf::Shader::isAvailable()) {
        m_selectionShader.setUniform("baseTexture", m_layers[srcIdx]->texture->getTexture());
        m_selectionShader.setUniform("selectionMask", m_selectionTexture->getTexture());
        states.shader = &m_selectionShader;
    }
    tempTex.draw(sf::Sprite(m_layers[srcIdx]->texture->getTexture()), states);
    tempTex.display();
    sf::Image cutPixels = tempTex.getTexture().copyToImage();

    // Erase the selection from all currently selected original layers
    sf::BlendMode eraseMode(
        sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
        sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
    );
    sf::Sprite maskSprite(m_selectionTexture->getTexture());

    for (int sel : m_selectedLayers) {
        if (m_layers[sel]->type == LayerType::Content) {
            auto eraseBeforeImg = std::make_unique<sf::Image>(m_layers[sel]->texture->getTexture().copyToImage());
            m_layers[sel]->texture->draw(maskSprite, sf::RenderStates(eraseMode));
            m_layers[sel]->texture->display();
            auto eraseAfterImg = std::make_unique<sf::Image>(m_layers[sel]->texture->getTexture().copyToImage());

            pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(eraseBeforeImg), std::move(eraseAfterImg), sel));
        }
    }

    // Create the new layer and paste the pixels
    addLayer();
    m_layers[m_activeLayerIndex]->name = "Cut Layer";

    auto pasteBeforeImg = std::make_unique<sf::Image>(m_layers[m_activeLayerIndex]->texture->getTexture().copyToImage());
    sf::Texture pasteTex;
    pasteTex.loadFromImage(cutPixels);
    m_layers[m_activeLayerIndex]->texture->draw(sf::Sprite(pasteTex), sf::RenderStates(sf::BlendNone));
    m_layers[m_activeLayerIndex]->texture->display();

    auto pasteAfterImg = std::make_unique<sf::Image>(m_layers[m_activeLayerIndex]->texture->getTexture().copyToImage());
    pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(pasteBeforeImg), std::move(pasteAfterImg), m_activeLayerIndex));

    endBatchCommand();
    renderComposite();

    // Clear the selection mask automatically after a Cut
    getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
    setSelectionActive(false);
}

void Canvas::importFromImage(const sf::Image& image, const std::string& layerName) {
    if (image.getSize().x == 0 || image.getSize().y == 0) return;

    sf::Texture loadedTex;
    if (!loadedTex.loadFromImage(image)) return;

    addLayer();
    auto& activeLayer = m_layers[m_activeLayerIndex];
    activeLayer->name = layerName;

    // Tell the UI to update the thumbnail
    activeLayer->boundsDirty = true;

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

bool Canvas::saveProject(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;

    // 1. Write the "Magic Signature" to identify the file format
    file.write("LAY3", 4);

    // 2. Write Canvas Dimensions
    file.write(reinterpret_cast<const char*>(&m_size.x), sizeof(m_size.x));
    file.write(reinterpret_cast<const char*>(&m_size.y), sizeof(m_size.y));

    // 3. Write Total Layer Count
    uint32_t layerCount = static_cast<uint32_t>(m_layers.size());
    file.write(reinterpret_cast<const char*>(&layerCount), sizeof(layerCount));

    // 4. Serialize every layer
    for (const auto& layer : m_layers) {
        // Name
        uint32_t nameLen = static_cast<uint32_t>(layer->name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(layer->name.c_str(), nameLen);

        // Types and Bools
        uint8_t typeInt = static_cast<uint8_t>(layer->type);
        file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));

        uint8_t vis = layer->visible ? 1 : 0;
        uint8_t alphaLock = layer->alphaLocked ? 1 : 0;
        uint8_t clip = layer->isClipped ? 1 : 0;
        uint8_t layerLock = layer->isLocked ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&vis), sizeof(vis));
        file.write(reinterpret_cast<const char*>(&alphaLock), sizeof(alphaLock));
        file.write(reinterpret_cast<const char*>(&clip), sizeof(clip));
        file.write(reinterpret_cast<const char*>(&layerLock), sizeof(layerLock));

        // Blend Math
        file.write(reinterpret_cast<const char*>(&layer->opacity), sizeof(layer->opacity));
        uint8_t blend = static_cast<uint8_t>(layer->blendMode);
        file.write(reinterpret_cast<const char*>(&blend), sizeof(blend));

        // Transforms & Hierarchy
        file.write(reinterpret_cast<const char*>(&layer->depth), sizeof(layer->depth));
        file.write(reinterpret_cast<const char*>(&layer->offset.x), sizeof(layer->offset.x));
        file.write(reinterpret_cast<const char*>(&layer->offset.y), sizeof(layer->offset.y));
        file.write(reinterpret_cast<const char*>(&layer->scale.x), sizeof(layer->scale.x));
        file.write(reinterpret_cast<const char*>(&layer->scale.y), sizeof(layer->scale.y));

        // 5. Compress the image to a PNG in memory, then write it to disk
        if (layer->type == LayerType::Content) {
            sf::Image img = layer->texture->getTexture().copyToImage();

            if (auto bufferOpt = img.saveToMemory("png")) {
                const auto& buffer = *bufferOpt; // Extract the vector from the optional

                // First, write the size of the compressed data so the loader knows how much to read
                uint32_t bufferSize = static_cast<uint32_t>(buffer.size());
                file.write(reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize));

                // Then, write the actual compressed bytes
                file.write(reinterpret_cast<const char*>(buffer.data()), bufferSize);
            }
        }
    }
    return true;
}

bool Canvas::loadProject(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    // 1. Check the Magic Signature
    char magic[4];
    file.read(magic, 4);
    if (std::strncmp(magic, "LAY3", 4) != 0) {
        return false;
    }

    // 2. Read Canvas Dimensions
    sf::Vector2u newSize;
    file.read(reinterpret_cast<char*>(&newSize.x), sizeof(newSize.x));
    file.read(reinterpret_cast<char*>(&newSize.y), sizeof(newSize.y));

    uint32_t layerCount;
    file.read(reinterpret_cast<char*>(&layerCount), sizeof(layerCount));

    // 3. Reset the entire canvas environment to adapt to the new project
    m_size = newSize;
    m_layers.clear();
    m_selectedLayers.clear();
    m_activeLayerIndex = 0;
    setSelectionActive(false);
    setSelectionLive(false);

    // Reallocate internal textures for the new size
    m_compositeTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_clippingTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_selectionTexture = std::make_unique<sf::RenderTexture>(m_size);
    m_selectionTexture->clear(sf::Color(0, 0, 0, 0));

    // Recompile the mask shader for the new resolution
    if (sf::Shader::isAvailable()) {
        try { m_selectionShader.setUniform("canvasSize", sf::Vector2f(static_cast<float>(m_size.x), static_cast<float>(m_size.y))); }
        catch (...) {}
    }

    // 4. Rebuild the layers from the binary data
    for (uint32_t i = 0; i < layerCount; ++i) {
        uint32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        std::string name(nameLen, '\0');
        file.read(&name[0], nameLen);

        uint8_t typeInt;
        file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
        LayerType type = static_cast<LayerType>(typeInt);

        auto layer = std::make_unique<Layer>(m_size, name, type);

        uint8_t vis, alphaLock, clip, layerLock;
        file.read(reinterpret_cast<char*>(&vis), sizeof(vis));
        file.read(reinterpret_cast<char*>(&alphaLock), sizeof(alphaLock));
        file.read(reinterpret_cast<char*>(&clip), sizeof(clip));
        file.read(reinterpret_cast<char*>(&layerLock), sizeof(layerLock));

        layer->visible = (vis != 0);
        layer->alphaLocked = (alphaLock != 0);
        layer->isClipped = (clip != 0);
        layer->isLocked = (layerLock != 0);

        file.read(reinterpret_cast<char*>(&layer->opacity), sizeof(layer->opacity));

        uint8_t blend;
        file.read(reinterpret_cast<char*>(&blend), sizeof(blend));
        layer->blendMode = static_cast<LayerBlendMode>(blend);

        file.read(reinterpret_cast<char*>(&layer->depth), sizeof(layer->depth));
        file.read(reinterpret_cast<char*>(&layer->offset.x), sizeof(layer->offset.x));
        file.read(reinterpret_cast<char*>(&layer->offset.y), sizeof(layer->offset.y));
        file.read(reinterpret_cast<char*>(&layer->scale.x), sizeof(layer->scale.x));
        file.read(reinterpret_cast<char*>(&layer->scale.y), sizeof(layer->scale.y));

        if (type == LayerType::Content) {
            // Read exactly how big the compressed PNG chunk is
            uint32_t bufferSize;
            file.read(reinterpret_cast<char*>(&bufferSize), sizeof(bufferSize));

            // Create a buffer of that exact size and read the bytes into it
            std::vector<std::uint8_t> buffer(bufferSize);
            file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);

            // Ask SFML to decompress the PNG memory back into a pixel image
            sf::Image img;
            if (img.loadFromMemory(buffer.data(), bufferSize)) {
                // Rebuild the SFML texture
                sf::Texture tex;
                tex.loadFromImage(img);
                layer->texture->clear(sf::Color(0, 0, 0, 0));
                layer->texture->draw(sf::Sprite(tex), sf::RenderStates(sf::BlendNone));
                layer->texture->display();
            }
        }

        m_layers.push_back(std::move(layer));
    }

    setActiveLayer(0);
    renderComposite();

    // Purge the undo stack so it doesn't try to apply old commands to the new file
    m_undoStack = UndoStack();

    return true;
}