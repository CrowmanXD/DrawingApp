#pragma once

#include "UndoCommand.h"
#include "Tile.h"
#include <vector>

class Canvas;

class StrokeUndoCommand : public UndoCommand {
public:
    StrokeUndoCommand(std::vector<Tile> before,
        std::vector<Tile> after);

    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    std::vector<Tile> m_before;
    std::vector<Tile> m_after;
};