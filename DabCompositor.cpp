#include "DabCompositor.h"
#include "Canvas.h"
#include <SFML/Graphics/Sprite.hpp>
#include <cstdint>

DabCompositor::DabCompositor() {
}

void DabCompositor::paintDab(Canvas& canvas, const Dab& dab, const sf::Texture& brushTexture, float baseSize) {
    sf::Sprite brush(brushTexture);
    brush.setOrigin(sf::Vector2f(32.f, 32.f));
    brush.setPosition(dab.position);

    // Scale brush by dab size
    float scale = (dab.size * baseSize) / 32.f;
    brush.setScale(sf::Vector2f(scale, scale));

    // Apply flow as opacity - low flow prevents caterpillar effect
    sf::Color dabColor = dab.color;
    dabColor.a = static_cast<std::uint8_t>(dab.color.a * dab.flow);
    brush.setColor(dabColor);

    // Use alpha blending - it works better with soft brush textures
    sf::RenderStates states(sf::BlendAlpha);
    canvas.draw(brush, dab.position, states);
}

void DabCompositor::paintDabs(Canvas& canvas, const std::vector<Dab>& dabs, const sf::Texture& brushTexture, float baseSize) {
    for (const auto& dab : dabs) {
        paintDab(canvas, dab, brushTexture, baseSize);
    }
}
