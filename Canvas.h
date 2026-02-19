#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <memory>

#include "UndoStack.h"

class Canvas {
public:
    explicit Canvas(sf::Vector2u size);

    void draw(const sf::Drawable& drawable, sf::Vector2f position);

    void beginStroke();
    void endStroke();

    void undo();
    void redo();

    sf::RenderTexture& getTexture();            // pentru desen
    const sf::Texture& getFinalTexture() const; // pentru afisare

    void clear(const sf::Color& color = sf::Color::White);

private:
    sf::RenderTexture m_texture;
    UndoStack m_undoStack;

    bool m_inStroke = false;
    sf::Image m_strokeBackup;
};
