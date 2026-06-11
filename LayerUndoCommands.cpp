#include "LayerUndoCommands.h"

// --- ADD LAYER COMMAND ---
AddLayerCommand::AddLayerCommand(int layerIndex) : m_layerIndex(layerIndex) {}

void AddLayerCommand::undo(Canvas& canvas) {
    // Take the layer off the canvas and hold it in the command
    m_savedLayer = canvas.removeLayer(m_layerIndex);
}

void AddLayerCommand::redo(Canvas& canvas) {
    // Put the layer back onto the canvas
    canvas.insertLayer(m_layerIndex, std::move(m_savedLayer));
}

// --- MOVE LAYER COMMAND ---
MoveLayerCommand::MoveLayerCommand(int fromIndex, int toIndex)
    : m_fromIndex(fromIndex), m_toIndex(toIndex) {
}

void MoveLayerCommand::undo(Canvas& canvas) {
    // To undo, we just move it back to where it came from
    canvas.moveLayer(m_toIndex, m_fromIndex);
}

void MoveLayerCommand::redo(Canvas& canvas) {
    // To redo, we execute the original move again
    canvas.moveLayer(m_fromIndex, m_toIndex);
}

// --- OPACITY CHANGE COMMAND ---
OpacityChangeCommand::OpacityChangeCommand(int layerIndex, float oldOpacity, float newOpacity)
    : m_layerIndex(layerIndex), m_oldOpacity(oldOpacity), m_newOpacity(newOpacity) {
}

void OpacityChangeCommand::undo(Canvas& canvas) {
    canvas.setLayerOpacity(m_layerIndex, m_oldOpacity);
}
void OpacityChangeCommand::redo(Canvas& canvas) {
    canvas.setLayerOpacity(m_layerIndex, m_newOpacity);
}

// --- BLEND MODE COMMAND ---
BlendModeChangeCommand::BlendModeChangeCommand(int layerIndex, int oldMode, int newMode)
    : m_layerIndex(layerIndex), m_oldMode(oldMode), m_newMode(newMode) {
}

void BlendModeChangeCommand::undo(Canvas& canvas) {
    if (m_layerIndex >= 0 && m_layerIndex < canvas.getLayers().size()) {
        canvas.getLayers()[m_layerIndex]->blendMode = static_cast<LayerBlendMode>(m_oldMode);
    }
}

void BlendModeChangeCommand::redo(Canvas& canvas) {
    if (m_layerIndex >= 0 && m_layerIndex < canvas.getLayers().size()) {
        canvas.getLayers()[m_layerIndex]->blendMode = static_cast<LayerBlendMode>(m_newMode);
    }
}

// --- REPARENT LAYER COMMAND ---
ReparentLayerCommand::ReparentLayerCommand(int oldIndex, int newIndex, int oldDepth, int newDepth)
    : m_oldIndex(oldIndex), m_newIndex(newIndex), m_oldDepth(oldDepth), m_newDepth(newDepth) {
}

void ReparentLayerCommand::undo(Canvas& canvas) {
    canvas.moveLayer(m_newIndex, m_oldIndex);
    // Directly target the moved layer to restore its depth
    canvas.getLayers()[m_oldIndex]->depth = m_oldDepth;
}

void ReparentLayerCommand::redo(Canvas& canvas) {
    canvas.moveLayer(m_oldIndex, m_newIndex);
    // Directly target the moved layer to apply its new depth
    canvas.getLayers()[m_newIndex]->depth = m_newDepth;
}

// --- DELETE LAYER COMMAND ---
DeleteLayerCommand::DeleteLayerCommand(int startIndex, std::vector<std::unique_ptr<Layer>> savedLayers, int oldActiveIndex)
    : m_startIndex(startIndex), m_count(savedLayers.size()), m_oldActiveIndex(oldActiveIndex), m_savedLayers(std::move(savedLayers)) {
}

void DeleteLayerCommand::undo(Canvas& canvas) {
    auto& layers = canvas.getLayers();
    // Move the layers back onto the canvas at their original position
    for (int i = 0; i < m_count; ++i) {
        layers.insert(layers.begin() + m_startIndex + i, std::move(m_savedLayers[i]));
    }
    m_savedLayers.clear(); // Clear the command's holding vector
    canvas.setActiveLayer(m_oldActiveIndex);
}

void DeleteLayerCommand::redo(Canvas& canvas) {
    auto& layers = canvas.getLayers();
    m_savedLayers.reserve(m_count);

    // Scoop the layers back out of the canvas
    for (int i = 0; i < m_count; ++i) {
        m_savedLayers.push_back(std::move(layers[m_startIndex]));
        layers.erase(layers.begin() + m_startIndex);
    }

    // Fix active layer safely
    int active = canvas.getActiveLayerIndex();
    if (active >= layers.size()) {
        canvas.setActiveLayer(std::max(0, static_cast<int>(layers.size()) - 1));
    }
}

// --- RENAME LAYER COMMAND ---
RenameLayerCommand::RenameLayerCommand(int layerIndex, const std::string& oldName, const std::string& newName)
    : m_layerIndex(layerIndex), m_oldName(oldName), m_newName(newName) {
}

void RenameLayerCommand::undo(Canvas& canvas) {
    canvas.setLayerName(m_layerIndex, m_oldName);
}
void RenameLayerCommand::redo(Canvas& canvas) {
    canvas.setLayerName(m_layerIndex, m_newName);
}

// --- VISIBILITY CHANGE COMMAND ---
VisibilityChangeCommand::VisibilityChangeCommand(int layerIndex, bool oldState, bool newState)
    : m_layerIndex(layerIndex), m_oldState(oldState), m_newState(newState) {
}

void VisibilityChangeCommand::undo(Canvas& canvas) {
    canvas.setLayerVisibility(m_layerIndex, m_oldState);
}
void VisibilityChangeCommand::redo(Canvas& canvas) {
    canvas.setLayerVisibility(m_layerIndex, m_newState);
}

// --- CLIP LAYER COMMAND ---
ClipLayerCommand::ClipLayerCommand(int layerIndex, bool oldState, bool newState)
    : m_layerIndex(layerIndex), m_oldState(oldState), m_newState(newState) {
}

void ClipLayerCommand::undo(Canvas& canvas) {
    if (m_layerIndex >= 0 && m_layerIndex < canvas.getLayers().size()) {
        canvas.getLayers()[m_layerIndex]->isClipped = m_oldState;
    }
}

void ClipLayerCommand::redo(Canvas& canvas) {
    if (m_layerIndex >= 0 && m_layerIndex < canvas.getLayers().size()) {
        canvas.getLayers()[m_layerIndex]->isClipped = m_newState;
    }
}

// --- MERGE LAYER COMMAND ---
MergeLayerCommand::MergeLayerCommand(int startIndex, int count, std::vector<std::unique_ptr<Layer>> originalLayers, std::unique_ptr<Layer> mergedLayer)
    : m_startIndex(startIndex), m_count(count), m_originalLayers(std::move(originalLayers)), m_mergedLayer(std::move(mergedLayer)) {
}

void MergeLayerCommand::undo(Canvas& canvas) {
    auto& layers = canvas.getLayers();
    if (m_startIndex < layers.size()) {
        // Pull the merged layer off the canvas and hold it in memory
        m_mergedLayer = std::move(layers[m_startIndex]);
        layers.erase(layers.begin() + m_startIndex);
    }
    // Re-insert the original layers back onto the canvas
    for (int i = 0; i < m_count; ++i) {
        layers.insert(layers.begin() + m_startIndex + i, std::move(m_originalLayers[i]));
    }
    m_originalLayers.clear();
    canvas.setActiveLayer(m_startIndex);
}

void MergeLayerCommand::redo(Canvas& canvas) {
    auto& layers = canvas.getLayers();
    // Scoop the original layers back into the command
    for (int i = 0; i < m_count; ++i) {
        m_originalLayers.push_back(std::move(layers[m_startIndex]));
        layers.erase(layers.begin() + m_startIndex);
    }
    // Drop the merged layer back onto the canvas
    layers.insert(layers.begin() + m_startIndex, std::move(m_mergedLayer));
    canvas.setActiveLayer(m_startIndex);
}

// --- BATCH COMMAND ---
void BatchCommand::addCommand(std::unique_ptr<UndoCommand> cmd) {
    m_commands.push_back(std::move(cmd));
}

void BatchCommand::undo(Canvas& canvas) {
    // Undo must execute in reverse order
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo(canvas);
    }
}

void BatchCommand::redo(Canvas& canvas) {
    // Redo executes in standard forward order
    for (auto& cmd : m_commands) {
        cmd->redo(canvas);
    }
}

bool BatchCommand::isEmpty() const {
    return m_commands.empty();
}

CropUndoCommand::CropUndoCommand(sf::Vector2u oldSize, sf::IntRect cropRect, std::vector<std::unique_ptr<sf::Image>> oldImages)
    : m_oldSize(oldSize), m_cropRect(cropRect), m_oldImages(std::move(oldImages)) {
}

void CropUndoCommand::undo(Canvas& canvas) {
    canvas.resizeQuietly(m_oldSize);
    auto& layers = canvas.getLayers();

    // Safely blast the old backed-up pixels back onto the resized layers
    for (size_t i = 0; i < layers.size(); ++i) {
        sf::Texture tex;
        if (tex.loadFromImage(*m_oldImages[i])) {
            layers[i]->texture->clear(sf::Color(0, 0, 0, 0));
            layers[i]->texture->draw(sf::Sprite(tex), sf::RenderStates(sf::BlendNone));
            layers[i]->texture->display();
        }
    }
}

void CropUndoCommand::redo(Canvas& canvas) {
    canvas.applyCrop(m_cropRect);
}