#include "PaintBucketTool.h"
#include "Canvas.h"
#include <cmath>

// Checks if two pixels are similar enough to be filled
bool PaintBucketTool::colorsMatch(const sf::Color& c1, const sf::Color& c2) const {
    if (m_tolerance == 0) return c1 == c2;

    // Explicit casts to int prevent unsigned underflow bugs
    return std::abs((int)c1.r - (int)c2.r) <= m_tolerance &&
        std::abs((int)c1.g - (int)c2.g) <= m_tolerance &&
        std::abs((int)c1.b - (int)c2.b) <= m_tolerance &&
        std::abs((int)c1.a - (int)c2.a) <= m_tolerance;
}

void PaintBucketTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    int startX = static_cast<int>(pos.x);
    int startY = static_cast<int>(pos.y);
    sf::Vector2u size = canvas.getSize();

    // Boundary check
    if (startX < 0 || startY < 0 || startX >= (int)size.x || startY >= (int)size.y) return;

    // 1. Download the current layer image from the GPU to the CPU
    sf::Image sourceImg = canvas.getActiveTexture().getTexture().copyToImage();
    sf::Color targetColor = sourceImg.getPixel({ (unsigned int)startX, (unsigned int)startY });

    // Prevent infinite loops if they click a pixel that is already the bucket color
    if (colorsMatch(targetColor, m_color)) return;

    // 2. Create a blank transparent overlay image
    sf::Image fillImg;
    fillImg.resize(size, sf::Color(0, 0, 0, 0));

    // 3. Queue-based Flood Fill Algorithm (BFS)
    std::queue<sf::Vector2i> q;
    std::vector<bool> visited(size.x * size.y, false); // Critical for speed

    q.push({ startX, startY });
    visited[startY * size.x + startX] = true;

    bool filledAny = false;

    // Directions for spreading: Up, Down, Left, Right
    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };

    while (!q.empty()) {
        auto p = q.front();
        q.pop();

        // Paint the pixel onto our transparent overlay
        fillImg.setPixel({ (unsigned int)p.x, (unsigned int)p.y }, m_color);
        filledAny = true;

        for (int i = 0; i < 4; ++i) {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];

            if (nx >= 0 && nx < (int)size.x && ny >= 0 && ny < (int)size.y) {
                int idx = ny * size.x + nx;
                if (!visited[idx]) {
                    visited[idx] = true;
                    if (colorsMatch(sourceImg.getPixel({ (unsigned int)nx, (unsigned int)ny }), targetColor)) {
                        q.push({ nx, ny });
                    }
                }
            }
        }
    }

    // 4. Bake the fill overlay to the canvas
    if (filledAny) {
        canvas.beginStroke(); // Start Undo command

        sf::Texture fillTex;
        if (fillTex.loadFromImage(fillImg)) {
            // Passing it to canvas.draw() automatically triggers:
            // 1. Selection mask clipping (via your shader)
            // 2. m_strokeModified = true (fixes ghost undos)
            // 3. Alpha blending seamlessly over the layer
            canvas.draw(sf::Sprite(fillTex), { 0.f, 0.f });
        }

        canvas.endStroke(); // Push to UndoStack
    }
}