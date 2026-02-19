#pragma once
#include "UndoCommand.h"
#include <vector>
#include <memory>

class UndoCommand;
class Canvas;

class UndoStack {
public:
    void push(std::unique_ptr<UndoCommand> cmd);

    void undo(Canvas& canvas);
    void redo(Canvas& canvas);

private:
    std::vector<std::unique_ptr<UndoCommand>> m_undo;
    std::vector<std::unique_ptr<UndoCommand>> m_redo;
};
