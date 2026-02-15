#pragma once

#include <SFML/Graphics.hpp>

class ImGuiLayer {
public:
    ImGuiLayer();
    ~ImGuiLayer();

    void init(sf::RenderWindow& window);
    void processEvent(sf::RenderWindow& window, const sf::Event& event);
    void update(sf::RenderWindow& window, sf::Time deltaTime);
    void render(sf::RenderWindow& window);
    void shutdown();

    bool wantsCaptureMouse() const;
};
