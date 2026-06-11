#pragma once

#include "UndoCommand.h"
#include <SFML/Graphics/Image.hpp>
#include <memory>

class Canvas;

class SelectionUndoCommand : public UndoCommand {
public:
    SelectionUndoCommand(std::unique_ptr<sf::Image> before,
        std::unique_ptr<sf::Image> after,
        bool beforeHasSelection,
        bool afterHasSelection);

    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    std::unique_ptr<sf::Image> m_before;
    std::unique_ptr<sf::Image> m_after;
    bool m_beforeHasSelection;
    bool m_afterHasSelection;
};