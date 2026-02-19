#pragma once

class Canvas;

class UndoCommand {
public:
    virtual ~UndoCommand() = default;

    virtual void undo(Canvas& canvas) = 0;
    virtual void redo(Canvas& canvas) = 0;
};
