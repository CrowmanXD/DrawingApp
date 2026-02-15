#include "UndoStack.h"
#include "UndoCommand.h"
#include "Canvas.h"

void UndoStack::push(std::unique_ptr<UndoCommand> cmd) {
    m_undo.push_back(std::move(cmd));
    m_redo.clear();
}

void UndoStack::undo(Canvas& canvas) {
    if (m_undo.empty()) return;

    auto cmd = std::move(m_undo.back());
    m_undo.pop_back();
    cmd->undo(canvas);
    m_redo.push_back(std::move(cmd));
}

void UndoStack::redo(Canvas& canvas) {
    if (m_redo.empty()) return;

    auto cmd = std::move(m_redo.back());
    m_redo.pop_back();
    cmd->redo(canvas);
    m_undo.push_back(std::move(cmd));
}