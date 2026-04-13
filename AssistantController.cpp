#include "AssistantController.h"

AssistantController::AssistantController(Canvas& canvas) : m_canvas(canvas) {}

void AssistantController::suggestLayer(const std::string& name) {
    // We use the same batch command pipeline the human UI uses
    // This guarantees the AI action is fully undoable via Ctrl+Z.
    m_canvas.beginBatchCommand();

    m_canvas.addLayer();
    if (auto* layer = m_canvas.getActiveLayer()) {
        layer->name = name + " (AI)";
    }

    m_canvas.endBatchCommand();
}

void AssistantController::applyStroke() {
    // Placeholder: Will eventually generate StrokeUndoCommands
}

void AssistantController::autoSelectRegion() {
    // Placeholder: Will eventually manipulate the Canvas FBO mask
}