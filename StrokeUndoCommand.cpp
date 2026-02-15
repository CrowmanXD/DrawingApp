#include "StrokeUndoCommand.h"
#include "Canvas.h"

StrokeUndoCommand::StrokeUndoCommand(
    std::vector<Tile> before,
    std::vector<Tile> after)
    : m_before(std::move(before)),
    m_after(std::move(after)) {
}

void StrokeUndoCommand::undo(Canvas& canvas) {
    for (const auto& t : m_before)
        t.apply(canvas);
}

void StrokeUndoCommand::redo(Canvas& canvas) {
    for (const auto& t : m_after)
        t.apply(canvas);
}
