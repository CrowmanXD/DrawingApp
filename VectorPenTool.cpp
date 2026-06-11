#include "VectorPenTool.h"
#include "Canvas.h"
#include "imgui.h" // Needed for the Right-Click Popup Menu
#include <cmath>

float dist(sf::Vector2f a, sf::Vector2f b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

void VectorPenTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    m_dragIndex = -1;
    m_dragType = 0;

    // Detect if the user is holding the ALT key
    bool isAltPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt);

    // 1. Check if we clicked an existing handle or node
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (dist(pos, m_nodes[i].handleOut) <= m_hitRadius) { m_dragIndex = i; m_dragType = 3; m_selectedNode = i; return; }
        if (dist(pos, m_nodes[i].handleIn) <= m_hitRadius) { m_dragIndex = i; m_dragType = 2; m_selectedNode = i; return; }

        if (dist(pos, m_nodes[i].pos) <= m_hitRadius) {
            m_selectedNode = i; // Select the node
            if (isAltPressed) {
                m_dragIndex = i;
                m_dragType = 1; // Only move the node if ALT is held
            }
            return;
        }
    }

    if (isAltPressed) {
        m_selectedNode = -1; // Deselect if clicking empty space with Alt
        return;
    }

    // 2. Clicked empty space -> Add new node
    PathNode newNode;
    newNode.pos = pos;
    newNode.handleIn = pos;
    newNode.handleOut = pos;
    newNode.isSmooth = true;
    m_nodes.push_back(newNode);

    m_dragIndex = m_nodes.size() - 1;
    m_selectedNode = m_dragIndex;
    m_dragType = 3;
}

void VectorPenTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    if (m_dragIndex == -1) return;

    PathNode& node = m_nodes[m_dragIndex];

    if (m_dragType == 1) { // Moving the Node (Alt+Drag)
        sf::Vector2f delta = pos - node.pos;
        node.pos = pos;
        node.handleIn += delta;
        node.handleOut += delta;
    }
    else if (m_dragType == 2) {
        node.handleIn = pos;
        if (node.isSmooth) node.handleOut = node.pos - (node.handleIn - node.pos);
    }
    else if (m_dragType == 3) {
        node.handleOut = pos;
        if (node.isSmooth) node.handleIn = node.pos - (node.handleOut - node.pos);
    }
}

void VectorPenTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    if (m_dragType == 3 && m_dragIndex != -1) {
        if (dist(m_nodes[m_dragIndex].pos, m_nodes[m_dragIndex].handleOut) < 2.0f) {
            m_nodes[m_dragIndex].isSmooth = false;
        }
    }
    m_dragIndex = -1;
    m_dragType = 0;
}

void VectorPenTool::onRightClick(Canvas& canvas, sf::Vector2f pos) {
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (dist(pos, m_nodes[i].pos) <= m_hitRadius) {
            m_contextNode = i;
            m_selectedNode = i; // Auto-select the node we right-clicked
            return;
        }
    }
}

void VectorPenTool::onKeyPress(Canvas& canvas, sf::Keyboard::Key key) {
    // Press Delete to remove the highlighted node
    if (key == sf::Keyboard::Key::Delete && m_selectedNode != -1) {
        m_nodes.erase(m_nodes.begin() + m_selectedNode);
        m_selectedNode = -1;
        if (m_nodes.empty()) m_isClosed = false;
    }
    else if (key == sf::Keyboard::Key::Enter && m_nodes.size() > 1) {
        bakePath(canvas);
    }
    else if (key == sf::Keyboard::Key::Escape) {
        m_nodes.clear();
        m_isClosed = false;
        m_selectedNode = -1;
    }
}

void VectorPenTool::onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation) {
    if (m_nodes.empty()) return;

    auto toScreen = [&](sf::Vector2f canvasPos) {
        return sf::Vector2f(canvasPos.x * zoom + offset.x, canvasPos.y * zoom + offset.y);
    };

    // 1. Draw the Blue Bezier Curve
    sf::VertexArray curveArr(sf::PrimitiveType::LineStrip);
    int limit = m_isClosed ? m_nodes.size() : m_nodes.size() - 1; // Loop back to start if closed

    for (int i = 0; i < limit; ++i) {
        sf::Vector2f p0 = m_nodes[i].pos;
        sf::Vector2f h0 = m_nodes[i].handleOut;

        int nextIdx = (i + 1) % m_nodes.size();
        sf::Vector2f h1 = m_nodes[nextIdx].handleIn;
        sf::Vector2f p1 = m_nodes[nextIdx].pos;

        int segments = 50;
        for (int j = 0; j <= segments; ++j) {
            float t = static_cast<float>(j) / segments;
            float u = 1.0f - t;
            sf::Vector2f pt = (u * u * u) * p0 + (3.0f * u * u * t) * h0 + (3.0f * u * t * t) * h1 + (t * t * t) * p1;
            curveArr.append(sf::Vertex(toScreen(pt), sf::Color(0, 150, 255)));
        }
    }
    window.draw(curveArr);

    // 2. Draw Nodes and Handles
    for (int i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        sf::Vector2f sPos = toScreen(node.pos);
        sf::Vector2f sIn = toScreen(node.handleIn);
        sf::Vector2f sOut = toScreen(node.handleOut);

        sf::Vertex lineIn[] = { sf::Vertex(sPos, sf::Color(150, 150, 150)), sf::Vertex(sIn, sf::Color(150, 150, 150)) };
        sf::Vertex lineOut[] = { sf::Vertex(sPos, sf::Color(150, 150, 150)), sf::Vertex(sOut, sf::Color(150, 150, 150)) };
        window.draw(lineIn, 2, sf::PrimitiveType::Lines);
        window.draw(lineOut, 2, sf::PrimitiveType::Lines);

        sf::CircleShape hShape(4.0f);
        hShape.setOrigin({ 4.0f, 4.0f });
        hShape.setFillColor(sf::Color::White);
        hShape.setOutlineColor(sf::Color(0, 150, 255));
        hShape.setOutlineThickness(1.0f);
        hShape.setPosition(sIn); window.draw(hShape);
        hShape.setPosition(sOut); window.draw(hShape);

        sf::RectangleShape nShape(sf::Vector2f(8.0f, 8.0f));
        nShape.setOrigin({ 4.0f, 4.0f });
        nShape.setFillColor(sf::Color::White);

        // Highlight the selected node in RED
        if (i == m_selectedNode) nShape.setOutlineColor(sf::Color::Red);
        else nShape.setOutlineColor(sf::Color(0, 150, 255));

        nShape.setOutlineThickness(1.0f);
        nShape.setPosition(sPos);
        window.draw(nShape);
    }

    // 3. Handle ImGui Right-Click Menu
    if (m_contextNode != -1) {
        ImGui::OpenPopup("NodeContextMenu");
        m_contextNode = -1; // Reset trigger
    }

    if (ImGui::BeginPopup("NodeContextMenu")) {
        if (m_selectedNode != -1) {
            ImGui::TextDisabled("Node Adjustments");
            ImGui::Separator();

            // If a node has no handles, this calculates its neighbor vectors and drops balanced handles in
            if (!m_nodes[m_selectedNode].isSmooth) {
                if (ImGui::MenuItem("Convert to Smooth Curve (Add Handles)")) {
                    PathNode& node = m_nodes[m_selectedNode];

                    // Default baseline direction vector
                    sf::Vector2f direction = { 30.0f, 0.0f };

                    // If it has neighbors, make the handles align with the path flow
                    if (m_nodes.size() > 1) {
                        int prevIdx = (m_selectedNode - 1 + m_nodes.size()) % m_nodes.size();
                        int nextIdx = (m_selectedNode + 1) % m_nodes.size();

                        // Vector from previous node to next node
                        sf::Vector2f pathVec = m_nodes[nextIdx].pos - m_nodes[prevIdx].pos;
                        float len = std::sqrt(pathVec.x * pathVec.x + pathVec.y * pathVec.y);
                        if (len > 0.0f) {
                            // Scale down the vector to make a manageable initial handle size (e.g., 25 pixels)
                            direction = (pathVec / len) * 25.0f;
                        }
                    }

                    node.handleOut = node.pos + direction;
                    node.handleIn = node.pos - direction;
                    node.isSmooth = true;
                }
            }

            if (ImGui::MenuItem("Delete Node")) {
                m_nodes.erase(m_nodes.begin() + m_selectedNode);
                m_selectedNode = -1;
                if (m_nodes.empty()) m_isClosed = false;
            }
            if (ImGui::MenuItem("Remove Both Handles")) {
                m_nodes[m_selectedNode].handleIn = m_nodes[m_selectedNode].pos;
                m_nodes[m_selectedNode].handleOut = m_nodes[m_selectedNode].pos;
                m_nodes[m_selectedNode].isSmooth = false;
            }
            if (ImGui::MenuItem("Remove In Handle")) {
                m_nodes[m_selectedNode].handleIn = m_nodes[m_selectedNode].pos;
                m_nodes[m_selectedNode].isSmooth = false;
            }
            if (ImGui::MenuItem("Remove Out Handle")) {
                m_nodes[m_selectedNode].handleOut = m_nodes[m_selectedNode].pos;
                m_nodes[m_selectedNode].isSmooth = false;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(m_isClosed ? "Open Curve" : "Close Curve")) {
            m_isClosed = !m_isClosed;
        }
        ImGui::EndPopup();
    }
}

void VectorPenTool::bakePath(Canvas& canvas) {
    if (m_nodes.empty()) return;
    canvas.beginStroke();

    int limit = m_isClosed ? m_nodes.size() : m_nodes.size() - 1;

    for (int i = 0; i < limit; ++i) {
        sf::Vector2f p0 = m_nodes[i].pos;
        sf::Vector2f h0 = m_nodes[i].handleOut;

        int nextIdx = (i + 1) % m_nodes.size();
        sf::Vector2f h1 = m_nodes[nextIdx].handleIn;
        sf::Vector2f p1 = m_nodes[nextIdx].pos;

        int segments = 50;
        sf::Vector2f prevPt = p0;
        for (int j = 1; j <= segments; ++j) {
            float t = static_cast<float>(j) / segments;
            float u = 1.0f - t;
            sf::Vector2f pt = (u * u * u) * p0 + (3.0f * u * u * t) * h0 + (3.0f * u * t * t) * h1 + (t * t * t) * p1;

            drawThickLineSegment(canvas, prevPt, pt);
            prevPt = pt;
        }
    }

    canvas.endStroke();
    m_nodes.clear();
    m_isClosed = false;
    m_selectedNode = -1;
}

void VectorPenTool::drawThickLineSegment(Canvas& canvas, sf::Vector2f start, sf::Vector2f end) {
    sf::Vector2f diff = end - start;
    float length = std::sqrt(diff.x * diff.x + diff.y * diff.y);
    if (length == 0.0f) return;

    sf::RectangleShape line(sf::Vector2f(length, m_size));
    line.setOrigin(sf::Vector2f(0.0f, m_size / 2.0f));
    line.setPosition(start);
    line.setFillColor(m_color);
    line.setRotation(sf::radians(std::atan2(diff.y, diff.x)));
    canvas.draw(line, start);

    sf::CircleShape joint(m_size / 2.0f);
    joint.setOrigin(sf::Vector2f(m_size / 2.0f, m_size / 2.0f));
    joint.setPosition(end);
    joint.setFillColor(m_color);
    canvas.draw(joint, end);
}