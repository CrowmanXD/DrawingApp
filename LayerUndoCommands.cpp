#include "LayerUndoCommands.h"

// --- ADD LAYER COMMAND ---
AddLayerCommand::AddLayerCommand(int layerIndex) : m_layerIndex(layerIndex) {}

// --- MOVE LAYER COMMAND ---
MoveLayerCommand::MoveLayerCommand(int fromIndex, int toIndex)
    : m_fromIndex(fromIndex), m_toIndex(toIndex) {
}

void MoveLayerCommand::undo(Canvas& canvas) {
    // To undo, we just move it back to where it came from!
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

void AddLayerCommand::undo(Canvas& canvas) {
    // Take the layer off the canvas and hold it in the command
    m_savedLayer = canvas.removeLayer(m_layerIndex);
}

void AddLayerCommand::redo(Canvas& canvas) {
    // Put the layer back onto the canvas
    canvas.insertLayer(m_layerIndex, std::move(m_savedLayer));
}

void OpacityChangeCommand::undo(Canvas& canvas) {
    if (m_layerIndex >= 0 && m_layerIndex < canvas.getLayers().size()) {
        canvas.getLayers()[m_layerIndex]->opacity = m_oldOpacity;
    }
}

void OpacityChangeCommand::redo(Canvas& canvas) {
    if (m_layerIndex >= 0 && m_layerIndex < canvas.getLayers().size()) {
        canvas.getLayers()[m_layerIndex]->opacity = m_newOpacity;
    }
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