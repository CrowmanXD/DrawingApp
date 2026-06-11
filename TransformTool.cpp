#include "TransformTool.h"
#include "Canvas.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>

void TransformTool::onActivate(Canvas& canvas) {
    auto layer = canvas.getActiveLayer();
    // Abort activation if the layer is a folder OR is locked
    if (layer->type == LayerType::Folder || layer->isLocked) return;

    // Reset transform states BEFORE generating the preview
    m_currentRotation = 0.f;
    m_activeHandle = -1;

    m_backupImage = layer->texture->getTexture().copyToImage();
    sf::Vector2u size = m_backupImage.getSize();

    // --- GRAB THE SELECTION MASK ---
    bool hasSelection = canvas.hasSelection();
    sf::Image maskImage;
    const uint8_t* maskPixels = nullptr;

    if (hasSelection) {
        maskImage = canvas.getSelectionTexture().getTexture().copyToImage();
        maskPixels = maskImage.getPixelsPtr();
    }

    int minX = size.x, minY = size.y, maxX = 0, maxY = 0;
    bool found = false;

    // --- FIND THE BOUNDING BOX ---
    const uint8_t* pixels = m_backupImage.getPixelsPtr();
    for (unsigned int y = 0; y < size.y; ++y) {
        for (unsigned int x = 0; x < size.x; ++x) {
            int index = (y * size.x + x) * 4 + 3; // Alpha channel

            if (pixels[index] > 0) { // If the layer has paint here
                if (!hasSelection || maskPixels[index] > 0) { // Either we have no selection, or the selection mask covers this pixel
                    if ((int)x < minX) minX = x;
                    if ((int)x > maxX) maxX = x;
                    if ((int)y < minY) minY = y;
                    if ((int)y > maxY) maxY = y;
                    found = true;
                }
            }
        }
    }

    if (!found) return; // Layer or selection is completely empty

    sf::IntRect origBounds({ minX, minY }, { maxX - minX + 1, maxY - minY + 1 });
    m_currentBounds = sf::FloatRect(origBounds);

    sf::Image floatingImg;
    floatingImg.resize(sf::Vector2u(origBounds.size.x, origBounds.size.y), sf::Color::Transparent);
    sf::Image bgImg = m_backupImage;

    // --- EXTRACT THE PIXELS ---
    for (int y = 0; y < origBounds.size.y; ++y) {
        for (int x = 0; x < origBounds.size.x; ++x) {
            unsigned int globalX = minX + x;
            unsigned int globalY = minY + y;
            sf::Color p = m_backupImage.getPixel({ globalX, globalY });

            if (hasSelection) {
                sf::Color m = maskImage.getPixel({ globalX, globalY });
                float maskAlpha = m.a / 255.0f;

                if (maskAlpha > 0.0f) {
                    // Split a Premultiplied Pixel so no holes are left behind
                    float srcA = p.a / 255.0f;
                    float floatA = srcA * maskAlpha;

                    // Extract the transformed chunk based on mask opacity
                    sf::Color floatPixel = p;
                    floatPixel.r = static_cast<std::uint8_t>(p.r * maskAlpha);
                    floatPixel.g = static_cast<std::uint8_t>(p.g * maskAlpha);
                    floatPixel.b = static_cast<std::uint8_t>(p.b * maskAlpha);
                    floatPixel.a = static_cast<std::uint8_t>(p.a * maskAlpha);

                    // Reconstruct the background behind the chunk to prevent ghost-holes
                    sf::Color bgPixel = sf::Color::Transparent;
                    if (floatA < 0.999f) { // Prevent division by zero
                        float denominator = 1.0f - floatA;
                        float factor = (1.0f - maskAlpha) / denominator;
                        bgPixel.r = static_cast<std::uint8_t>(std::clamp(p.r * factor, 0.0f, 255.0f));
                        bgPixel.g = static_cast<std::uint8_t>(std::clamp(p.g * factor, 0.0f, 255.0f));
                        bgPixel.b = static_cast<std::uint8_t>(std::clamp(p.b * factor, 0.0f, 255.0f));
                        bgPixel.a = static_cast<std::uint8_t>(std::clamp(p.a * factor, 0.0f, 255.0f));
                    }

                    floatingImg.setPixel({ (unsigned int)x, (unsigned int)y }, floatPixel);
                    bgImg.setPixel({ globalX, globalY }, bgPixel);
                }
                else {
                    // Pixel is fully outside the mask, so the floating layer gets nothing
                    floatingImg.setPixel({ (unsigned int)x, (unsigned int)y }, sf::Color::Transparent);
                }
            }
            else {
                // NO SELECTION ACTIVE: Standard behavior (Grab everything, clear background)
                floatingImg.setPixel({ (unsigned int)x, (unsigned int)y }, p);
                bgImg.setPixel({ globalX, globalY }, sf::Color::Transparent);
            }
        }
    }

    m_floatingTexture.loadFromImage(floatingImg);
    m_floatingTexture.setSmooth(true);
    m_backgroundTexture.loadFromImage(bgImg);

    m_isActive = true;

    // Clear the mask so the marching ants vanish while dragging the drawing
    if (hasSelection) {
        canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
        canvas.setSelectionActive(false);
    }

    renderLivePreview(canvas);
}

void TransformTool::onDeactivate(Canvas& canvas) {
    if (m_isActive) applyTransform(canvas); // Auto-apply if they switch tools
}

void TransformTool::renderLivePreview(Canvas& canvas) {
    if (!m_isActive) return;
    auto layer = canvas.getActiveLayer();

    // Clear the active layer completely
    layer->texture->clear(sf::Color::Transparent);

    // Draw the static background (everything else on the layer)
    layer->texture->draw(sf::Sprite(m_backgroundTexture), sf::RenderStates(sf::BlendNone));

    // Draw the floating pixels stretched to match the mouse drag box
    sf::Sprite floating(m_floatingTexture);
    // Rotational Math
    floating.setOrigin({ m_floatingTexture.getSize().x / 2.f, m_floatingTexture.getSize().y / 2.f });

    // Because we moved the origin, we must move the position to the center of the box
    floating.setPosition({ m_currentBounds.position.x + m_currentBounds.size.x / 2.f,
                           m_currentBounds.position.y + m_currentBounds.size.y / 2.f });

    floating.setRotation(sf::degrees(m_currentRotation));

    floating.setScale(sf::Vector2f(
        m_currentBounds.size.x / m_floatingTexture.getSize().x,
        m_currentBounds.size.y / m_floatingTexture.getSize().y
    ));

    sf::BlendMode premultipliedBlend(
        sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add,
        sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha, sf::BlendMode::Equation::Add
    );
    layer->texture->draw(floating, sf::RenderStates(premultipliedBlend));

    layer->texture->display();
}

void TransformTool::applyTransform(Canvas& canvas) {
    // Restore the ORIGINAL unmodified image to the layer to prepare for the Undo Backup
    sf::Texture temp;
    if (temp.loadFromImage(m_backupImage)) {
        canvas.getActiveLayer()->texture->clear(sf::Color::Transparent);
        canvas.getActiveLayer()->texture->draw(sf::Sprite(temp), sf::RenderStates(sf::BlendNone));
        canvas.getActiveLayer()->texture->display();
    }

    // Trigger your standard Stroke Backup
    canvas.beginStroke();

    // Force the Canvas engine's "stroke modified" flag to true so it doesn't discard our Undo
    sf::RectangleShape invisiblePixel({ 1.f, 1.f });
    invisiblePixel.setFillColor(sf::Color(0, 0, 0, 0));
    canvas.draw(invisiblePixel, { 0.f, 0.f });

    renderLivePreview(canvas); // Bake the transformation permanently
    canvas.endStroke();

    m_isActive = false;
    m_activeHandle = -1;
}

int TransformTool::getHoveredHandle(sf::Vector2f pos, float zoom) {
    float x = m_currentBounds.position.x;
    float y = m_currentBounds.position.y;
    float w = m_currentBounds.size.x;
    float h = m_currentBounds.size.y;

    sf::Vector2f pts[8] = {
        {x, y}, {x + w / 2.f, y}, {x + w, y},
        {x + w, y + h / 2.f}, {x + w, y + h},
        {x + w / 2.f, y + h}, {x, y + h}, {x, y + h / 2.f}
    };

    float hitRadius = 15.0f / zoom;
    for (int i = 0; i < 8; ++i) {
        float dx = pos.x - pts[i].x;
        float dy = pos.y - pts[i].y;
        if (std::sqrt(dx * dx + dy * dy) <= hitRadius) return i;
    }

    if (m_currentBounds.contains(pos)) return 8; // Moving the center
    return -1;
}

void TransformTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isActive) {
        onActivate(canvas);
        if (!m_isActive) return;
    }

    sf::Vector2f center(m_currentBounds.position.x + m_currentBounds.size.x / 2.f,
        m_currentBounds.position.y + m_currentBounds.size.y / 2.f);

    sf::Transform inverse;
    inverse.rotate(sf::degrees(-m_currentRotation), center);
    sf::Vector2f localPos = inverse.transformPoint(pos);

    float hitRadius = 15.f / m_currentZoom;
    sf::Vector2f positions[8] = {
        {m_currentBounds.position.x, m_currentBounds.position.y},
        {m_currentBounds.position.x + m_currentBounds.size.x / 2.f, m_currentBounds.position.y},
        {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y},
        {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y + m_currentBounds.size.y / 2.f},
        {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y + m_currentBounds.size.y},
        {m_currentBounds.position.x + m_currentBounds.size.x / 2.f, m_currentBounds.position.y + m_currentBounds.size.y},
        {m_currentBounds.position.x, m_currentBounds.position.y + m_currentBounds.size.y},
        {m_currentBounds.position.x, m_currentBounds.position.y + m_currentBounds.size.y / 2.f}
    };

    m_activeHandle = -1;
    for (int i = 0; i < 8; ++i) {
        float dx = localPos.x - positions[i].x;
        float dy = localPos.y - positions[i].y;
        if (std::sqrt(dx * dx + dy * dy) <= hitRadius) {
            m_activeHandle = i;
            break; // Grabbed a scale handle
        }
    }

    // Strict Corner-Only Rotation Zone
    if (m_activeHandle == -1) {
        float rotateRadius = 25.f / m_currentZoom; // Range of the rotation zone

        // Calculate exactly how far OUTSIDE the box the cursor is
        float dxLeft = m_currentBounds.position.x - localPos.x;
        float dxRight = localPos.x - (m_currentBounds.position.x + m_currentBounds.size.x);
        float dyTop = m_currentBounds.position.y - localPos.y;
        float dyBottom = localPos.y - (m_currentBounds.position.y + m_currentBounds.size.y);

        // ONLY trigger if the cursor is strictly diagonally OUTWARD from a corner
        if (dxLeft > 0 && dyTop > 0 && std::sqrt(dxLeft * dxLeft + dyTop * dyTop) <= rotateRadius) m_activeHandle = 8;
        else if (dxRight > 0 && dyTop > 0 && std::sqrt(dxRight * dxRight + dyTop * dyTop) <= rotateRadius) m_activeHandle = 8;
        else if (dxRight > 0 && dyBottom > 0 && std::sqrt(dxRight * dxRight + dyBottom * dyBottom) <= rotateRadius) m_activeHandle = 8;
        else if (dxLeft > 0 && dyBottom > 0 && std::sqrt(dxLeft * dxLeft + dyBottom * dyBottom) <= rotateRadius) m_activeHandle = 8;
    }

    /// SAVE DRAG STATE
    if (m_activeHandle == 8) {
        // Grabbed the ROTATION zone
        m_dragStartAngle = std::atan2(pos.y - center.y, pos.x - center.x) * 180.f / 3.14159265f;
        m_dragStartRotation = m_currentRotation;
    }
    else if (m_activeHandle != -1) {
        // Grabbed a SCALE handle
        m_dragStartPos = localPos;
        m_dragStartBounds = m_currentBounds;
    }
    else if (m_currentBounds.contains(localPos)) {
        // Grabbed the MIDDLE to TRANSLATE
        m_activeHandle = 9;

        // Save the raw 'pos' here, not the rotated 'localPos'. 
        // This ensures the layer tracks your mouse 1:1, even if it is upside down
        m_dragStartPos = pos;
        m_dragStartBounds = m_currentBounds;
    }
}

void TransformTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    sf::Vector2f center(m_currentBounds.position.x + m_currentBounds.size.x / 2.f,
        m_currentBounds.position.y + m_currentBounds.size.y / 2.f);

    // ---  EXECUTE TRANSLATION DRAG ---
    if (m_activeHandle == 9) {
        // Move the box physically across the screen using un-rotated screen coordinates
        m_currentBounds.position = m_dragStartBounds.position + (pos - m_dragStartPos);
        renderLivePreview(canvas);
        return;
    }

    // --- EXECUTE ROTATION DRAG ---
    if (m_activeHandle == 8) {
        // Calculate the physical angle of the mouse to spin the layer
        float currentAngle = std::atan2(pos.y - center.y, pos.x - center.x) * 180.f / 3.14159265f;
        m_currentRotation = m_dragStartRotation + (currentAngle - m_dragStartAngle);
        renderLivePreview(canvas);
        return;
    }

    // --- HOVER DETECTION FOR CURSOR ---
    m_isHoveringRotation = false;
    if (m_activeHandle == -1) {
        sf::Transform inverse;
        inverse.rotate(sf::degrees(-m_currentRotation), center);
        sf::Vector2f localPos = inverse.transformPoint(pos);

        // Check Scale Handles First
        float hitRadius = 15.f / m_currentZoom;
        bool hoveringScaleHandle = false;
        sf::Vector2f positions[8] = {
            {m_currentBounds.position.x, m_currentBounds.position.y},
            {m_currentBounds.position.x + m_currentBounds.size.x / 2.f, m_currentBounds.position.y},
            {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y},
            {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y + m_currentBounds.size.y / 2.f},
            {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y + m_currentBounds.size.y},
            {m_currentBounds.position.x + m_currentBounds.size.x / 2.f, m_currentBounds.position.y + m_currentBounds.size.y},
            {m_currentBounds.position.x, m_currentBounds.position.y + m_currentBounds.size.y},
            {m_currentBounds.position.x, m_currentBounds.position.y + m_currentBounds.size.y / 2.f}
        };
        for (int i = 0; i < 8; ++i) {
            float dx = localPos.x - positions[i].x;
            float dy = localPos.y - positions[i].y;
            if (std::sqrt(dx * dx + dy * dy) <= hitRadius) {
                hoveringScaleHandle = true;
                break;
            }
        }

        // ONLY show rotation cursor if NOT hovering a scale handle, and strictly diagonally outward
        if (!hoveringScaleHandle) {
            float rotateRadius = 25.f / m_currentZoom;
            float dxLeft = m_currentBounds.position.x - localPos.x;
            float dxRight = localPos.x - (m_currentBounds.position.x + m_currentBounds.size.x);
            float dyTop = m_currentBounds.position.y - localPos.y;
            float dyBottom = localPos.y - (m_currentBounds.position.y + m_currentBounds.size.y);

            if (dxLeft > 0 && dyTop > 0 && std::sqrt(dxLeft * dxLeft + dyTop * dyTop) <= rotateRadius) m_isHoveringRotation = true;
            else if (dxRight > 0 && dyTop > 0 && std::sqrt(dxRight * dxRight + dyTop * dyTop) <= rotateRadius) m_isHoveringRotation = true;
            else if (dxRight > 0 && dyBottom > 0 && std::sqrt(dxRight * dxRight + dyBottom * dyBottom) <= rotateRadius) m_isHoveringRotation = true;
            else if (dxLeft > 0 && dyBottom > 0 && std::sqrt(dxLeft * dxLeft + dyBottom * dyBottom) <= rotateRadius) m_isHoveringRotation = true;
        }
    }
    
    if (m_activeHandle == -1 || !m_isActive) return;

    // Calculate the center of the box from WHEN THE DRAG STARTED so the coordinate grid doesn't drift
    sf::Vector2f startCenter(
        m_dragStartBounds.position.x + m_dragStartBounds.size.x / 2.f,
        m_dragStartBounds.position.y + m_dragStartBounds.size.y / 2.f
    );

    // Un-rotate the current mouse position using that locked center point
    sf::Transform inverse;
    inverse.rotate(sf::degrees(-m_currentRotation), startCenter);
    sf::Vector2f currentLocalPos = inverse.transformPoint(pos);

    sf::Vector2f delta = currentLocalPos - m_dragStartPos;
    // --- SEPARATE LOCAL RESIZING FROM GLOBAL POSITIONING ---
    // Track exactly how much each specific edge of the box is being pushed or pulled
    float dLeft = 0.f, dRight = 0.f, dTop = 0.f, dBottom = 0.f;

    if (m_activeHandle == 0 || m_activeHandle == 6 || m_activeHandle == 7) dLeft = delta.x;
    if (m_activeHandle == 2 || m_activeHandle == 3 || m_activeHandle == 4) dRight = delta.x;
    if (m_activeHandle == 0 || m_activeHandle == 1 || m_activeHandle == 2) dTop = delta.y;
    if (m_activeHandle == 4 || m_activeHandle == 5 || m_activeHandle == 6) dBottom = delta.y;

    // Clamp the movements so the box can never shrink below 1 pixel wide/tall
    float minSize = 1.0f;
    if (m_dragStartBounds.size.x + dRight - dLeft < minSize) {
        if (dLeft != 0.f) dLeft = m_dragStartBounds.size.x + dRight - minSize;
        if (dRight != 0.f) dRight = minSize - m_dragStartBounds.size.x + dLeft;
    }
    if (m_dragStartBounds.size.y + dBottom - dTop < minSize) {
        if (dTop != 0.f) dTop = m_dragStartBounds.size.y + dBottom - minSize;
        if (dBottom != 0.f) dBottom = minSize - m_dragStartBounds.size.y + dTop;
    }

    // Calculate the true new Local Dimensions
    float newWidth = m_dragStartBounds.size.x + dRight - dLeft;
    float newHeight = m_dragStartBounds.size.y + dBottom - dTop;

    // Calculate exactly how far the CENTER of the box moved in LOCAL space
    // (A box's center moves exactly half the distance of the edge that was pulled)
    sf::Vector2f localCenterShift((dLeft + dRight) / 2.f, (dTop + dBottom) / 2.f);

    // Rotate that local movement into GLOBAL space
    // (This translates the directional pull into exact screen coordinates)
    sf::Transform rot;
    rot.rotate(sf::degrees(m_currentRotation));
    sf::Vector2f globalCenterShift = rot.transformPoint(localCenterShift);

    // Apply the global shift to the locked starting center to get the true new global center
    sf::Vector2f newCenter = startCenter + globalCenterShift;

    // Rebuild the un-rotated bounding box around this mathematically perfect new center
    m_currentBounds.size = { newWidth, newHeight };
    m_currentBounds.position = newCenter - sf::Vector2f(newWidth / 2.f, newHeight / 2.f);

    renderLivePreview(canvas);
}

void TransformTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    m_activeHandle = -1;
}

void TransformTool::onKeyPress(Canvas& canvas, sf::Keyboard::Key key) {
    if (key == sf::Keyboard::Key::Escape && m_isActive) {
        // Cancel the transform
        sf::Texture temp;
        if (temp.loadFromImage(m_backupImage)) {
            canvas.getActiveLayer()->texture->clear(sf::Color::Transparent);
            canvas.getActiveLayer()->texture->draw(sf::Sprite(temp), sf::RenderStates(sf::BlendNone));
            canvas.getActiveLayer()->texture->display();
        }
        m_isActive = false;

        // Wipe lingering states so the next click is fresh
        m_currentRotation = 0.f;
        m_activeHandle = -1;
    }
    else if (key == sf::Keyboard::Key::Enter && m_isActive && m_activeHandle == -1) {
        applyTransform(canvas);
    }
}

void TransformTool::onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation) {
    if (!m_isActive) return;

    // Calculate the exact center of the bounding box
    sf::Vector2f center(
        m_currentBounds.position.x + m_currentBounds.size.x / 2.f,
        m_currentBounds.position.y + m_currentBounds.size.y / 2.f
    );

    // Setup the RenderStates to automatically apply Camera View and Rotation
    sf::Transform transform;

    sf::Vector2f screenCenter(window.getSize().x / 2.f, window.getSize().y / 2.f);
    transform.translate(screenCenter);
    transform.rotate(sf::degrees(workspaceRotation));
    transform.translate(-screenCenter);
    transform.translate(offset);               // Apply Camera Pan
    transform.scale(sf::Vector2f(zoom, zoom)); // Apply Camera Zoom

    // Apply the Rotation mathematically around the center point
    transform.rotate(sf::degrees(m_currentRotation), center);

    sf::RenderStates states(transform);

    // Draw the main Bounding Box
    sf::RectangleShape box;
    box.setPosition(m_currentBounds.position);
    box.setSize(m_currentBounds.size);
    box.setFillColor(sf::Color::Transparent);
    box.setOutlineColor(sf::Color::White);

    // Divide by zoom so the line stays exactly 1 pixel thick no matter how far you zoom in
    box.setOutlineThickness(1.0f / zoom);
    window.draw(box, states);

    // Draw the 8 Scale Handles
    float handleSize = 8.0f / zoom; // Keeps handles a consistent visual size
    sf::Vector2f handleOffset(handleSize / 2.f, handleSize / 2.f);

    sf::Vector2f positions[8] = {
        {m_currentBounds.position.x, m_currentBounds.position.y}, // 0: Top-Left
        {m_currentBounds.position.x + m_currentBounds.size.x / 2.f, m_currentBounds.position.y}, // 1: Top-Center
        {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y}, // 2: Top-Right
        {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y + m_currentBounds.size.y / 2.f}, // 3: Right-Center
        {m_currentBounds.position.x + m_currentBounds.size.x, m_currentBounds.position.y + m_currentBounds.size.y}, // 4: Bottom-Right
        {m_currentBounds.position.x + m_currentBounds.size.x / 2.f, m_currentBounds.position.y + m_currentBounds.size.y}, // 5: Bottom-Center
        {m_currentBounds.position.x, m_currentBounds.position.y + m_currentBounds.size.y}, // 6: Bottom-Left
        {m_currentBounds.position.x, m_currentBounds.position.y + m_currentBounds.size.y / 2.f} // 7: Left-Center
    };

    sf::RectangleShape handleRect;
    handleRect.setSize({ handleSize, handleSize });
    handleRect.setFillColor(sf::Color::White);
    handleRect.setOutlineColor(sf::Color::Black);
    handleRect.setOutlineThickness(1.0f / zoom);

    // Draw all 8 handles using the rotated states
    for (int i = 0; i < 8; ++i) {
        handleRect.setPosition(positions[i] - handleOffset);
        window.draw(handleRect, states);
    }

    if (m_isHoveringRotation || m_activeHandle == 8) {
        if (auto cursor = sf::Cursor::createFromSystem(sf::Cursor::Type::Cross)) {
            window.setMouseCursor(*cursor);
        }
    }
    else {
        if (auto cursor = sf::Cursor::createFromSystem(sf::Cursor::Type::Arrow)) {
            window.setMouseCursor(*cursor);
        }
    }
}

void TransformTool::onRightClick(Canvas& canvas, sf::Vector2f pos) {
    if (m_isActive) m_showContextMenu = true;
}

void TransformTool::rotate90(bool clockwise) {
    m_activeHandle = -1; // Cancel any active dragging

    sf::Image oldImg = m_floatingTexture.copyToImage();
    unsigned int w = oldImg.getSize().x;
    unsigned int h = oldImg.getSize().y;

    sf::Image newImg;
    newImg.resize(sf::Vector2u(h, w), sf::Color::Transparent);

    // Swap the pixels mathematically
    for (unsigned int y = 0; y < h; ++y) {
        for (unsigned int x = 0; x < w; ++x) {
            sf::Color p = oldImg.getPixel({ x, y });
            if (clockwise) newImg.setPixel({ h - 1 - y, x }, p);
            else newImg.setPixel({ y, w - 1 - x }, p);
        }
    }
    m_floatingTexture.loadFromImage(newImg);

    // Swap bounding box dimensions and precisely re-center it
    sf::Vector2f center = m_currentBounds.position + m_currentBounds.size / 2.0f;
    m_currentBounds.size = { m_currentBounds.size.y, m_currentBounds.size.x };
    m_currentBounds.position = center - m_currentBounds.size / 2.0f;
}

void TransformTool::flip(bool horizontal) {
    m_activeHandle = -1; // Cancel any active dragging

    sf::Image oldImg = m_floatingTexture.copyToImage();
    unsigned int w = oldImg.getSize().x;
    unsigned int h = oldImg.getSize().y;

    sf::Image newImg;
    newImg.resize(sf::Vector2u(w, h), sf::Color::Transparent);

    // Mirror the pixels mathematically
    for (unsigned int y = 0; y < h; ++y) {
        for (unsigned int x = 0; x < w; ++x) {
            sf::Color p = oldImg.getPixel({ x, y });
            if (horizontal) newImg.setPixel({ w - 1 - x, y }, p);
            else newImg.setPixel({ x, h - 1 - y }, p);
        }
    }
    m_floatingTexture.loadFromImage(newImg);
}