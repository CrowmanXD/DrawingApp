#pragma once

#include <SFML/Graphics.hpp>
#include <map>

class Application;

class ImGuiLayer {
public:
    ImGuiLayer();
    ~ImGuiLayer();

    void init(sf::RenderWindow& window);
    void loadIcons();
    void processEvent(sf::RenderWindow& window, const sf::Event& event);
    void update(sf::RenderWindow& window, sf::Time deltaTime, Application& app);
    void render(sf::RenderWindow& window);
    void shutdown();

    bool wantsCaptureMouse() const;
    bool wantsCaptureKeyboard() const;
private:
    std::map<std::string, sf::Texture> m_icons; // Stores all the GPU textures
};
