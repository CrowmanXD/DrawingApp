#pragma once

#include "UndoCommand.h"
#include <SFML/Graphics/Image.hpp>
#include <memory>

class Canvas;

class StrokeUndoCommand : public UndoCommand {
public:
    StrokeUndoCommand(std::unique_ptr<sf::Image> before,
                      std::unique_ptr<sf::Image> after);

    void undo(Canvas& canvas) override;
    void redo(Canvas& canvas) override;

private:
    std::unique_ptr<sf::Image> m_before;
    std::unique_ptr<sf::Image> m_after;
};