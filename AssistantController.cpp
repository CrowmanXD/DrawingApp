#include "AssistantController.h"
#include "LayerUndoCommands.h"
#include "StrokeUndoCommand.h"
#include "json.hpp"
#include "StringUtils.h"
#include "ConfigManager.h"
#include "Base64Codec.h"
#include "BlendModeUtils.h"
#include "HttpClient.h"
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <chrono>

AssistantController::AssistantController(Canvas& canvas) : m_canvas(canvas) {
    // Controller is created
    m_isAlive = std::make_shared<std::atomic<bool>>(true);
}

AssistantController::~AssistantController() {
    // Controller is being destroyed, flip the token so detached threads know to abort
    *m_isAlive = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

AssistantState AssistantController::getState() const {
    return m_state.load();
}

std::string AssistantController::getLastError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

AssistantContext AssistantController::buildContextSnapshot() const {
    AssistantContext context;
    context.canvasSize = m_canvas.getSize();
    context.activeLayerIndex = m_canvas.getActiveLayerIndex();
    context.hasSelection = m_canvas.hasSelection();

    const auto& layers = m_canvas.getLayers();
    context.layers.reserve(layers.size());
    for (const auto& layer : layers) {
        if (!layer) continue;
        context.layers.push_back(AssistantLayerInfo{
            layer->name,
            layer->visible,
			layer->isLocked,
			layer->alphaLocked,
            layer->isClipped
            });
    }

    return context;
}

void AssistantController::requestAIHelp(const std::string& prompt) {
    if (m_state.load() == AssistantState::Thinking) return;
    if (!m_backend) return;

    // Add to chat history first
    m_chatHistory.push_back({ "You", prompt });

    AssistantContext contextSnapshot = buildContextSnapshot();

    // Copy the history so the background thread can safely read it
    std::vector<ChatMessage> historyCopy = m_chatHistory;

    m_state.store(AssistantState::Thinking);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        m_pendingResponse = AIResponse{};
    }

    if (m_workerThread.joinable()) m_workerThread.join();

    m_workerThread = std::thread([this, contextSnapshot = std::move(contextSnapshot), historyCopy = std::move(historyCopy)]() {
        try {
            // Pass the history to the backend
            auto result = m_backend->requestAction(contextSnapshot, historyCopy);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingResponse = std::move(result);
            m_state.store(AssistantState::Applying);
        }
        catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastError = e.what();
            m_state.store(AssistantState::Error);
        }
    });
}

void AssistantController::processPendingActions() {

    // Process standard AI text/JSON actions ONLY if the state is Applying
    if (m_state.load() == AssistantState::Applying) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // --- LOG AI RESPONSE AND EXECUTE ACTIONS ---
        m_chatHistory.push_back({ "AI", m_pendingResponse.message });

        if (!m_pendingResponse.actions.empty()) {
            m_canvas.beginBatchCommand();
            for (const auto& op : m_pendingResponse.actions) executeAction(op);
            m_canvas.endBatchCommand();
        }

        // Set back to Idle to not process the same JSON twice
        m_state.store(AssistantState::Idle);
    }

    // Process the Image Queue ALWAYS, independent of the AI state
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        if (!m_readyImages.empty()) {
            m_canvas.beginBatchCommand();

            for (auto& pair : m_readyImages) {
                int layerIdx = pair.first;
                sf::Image& img = pair.second;

                if (layerIdx >= 0 && layerIdx < m_canvas.getLayers().size()) {
                    auto& targetLayer = m_canvas.getLayers()[layerIdx];

                    // Save the "Before" state for the Undo Stack
                    auto beforeImg = std::make_unique<sf::Image>(targetLayer->texture->getTexture().copyToImage());

                    // Convert the Image to a Texture and paint it onto the layer
                    sf::Texture tex;
                    tex.loadFromImage(img);
                    targetLayer->texture->clear(sf::Color(0, 0, 0, 0));
                    // Mathematically stretch the AI image to fill the canvas
                    sf::Sprite sprite(tex);
                    float scaleX = static_cast<float>(m_canvas.getSize().x) / tex.getSize().x;
                    float scaleY = static_cast<float>(m_canvas.getSize().y) / tex.getSize().y;
                    sprite.setScale({scaleX, scaleY});

                    targetLayer->texture->draw(sprite, sf::RenderStates(sf::BlendNone));
                    targetLayer->texture->display();

                    // Save the "After" state and trigger UI updates
                    auto afterImg = std::make_unique<sf::Image>(targetLayer->texture->getTexture().copyToImage());
                    m_canvas.pushUndoCommand(std::make_unique<StrokeUndoCommand>(std::move(beforeImg), std::move(afterImg), layerIdx));
                    targetLayer->boundsDirty = true;
                }
            }
            m_canvas.endBatchCommand();
            m_canvas.renderComposite(); // Force screen update
            m_readyImages.clear();      // Empty the queue
        }
    }
}

void AssistantController::executeAction(const AIOperation& op) {
    // std::visit unpacks the variant and routes to the matching handler
    struct Dispatcher {
        AssistantController* self;
        void operator()(const AddLayerAction& arg) const { self->handleAddLayerAction(arg); }
        void operator()(const DeleteLayerAction& arg) const { self->handleDeleteLayerAction(arg); }
        void operator()(const ModifyLayerAction& arg) const { self->handleModifyLayerAction(arg); }
        void operator()(const MoveLayerAction& arg) const { self->handleMoveLayerAction(arg); }
        void operator()(const AddFolderAction& arg) const { self->handleAddFolderAction(arg); }
        void operator()(const SelectLayerAction& arg) const { self->handleSelectLayerAction(arg); }
        void operator()(const GenerateImageAction& arg) const { self->handleGenerateImageAction(arg); }
        void operator()(const EditImageAction& arg) const { self->handleEditImageAction(arg); }
    };
    std::visit(Dispatcher{this}, op);
}

// Resolves a target layer index from the AI-provided index and/or name, with safety checks to prevent hallucinated indices.
std::vector<int> AssistantController::resolveLayerIndices(int providedIndex, const std::string& providedName) {
    auto& layers = m_canvas.getLayers();
    std::vector<int> targetIndices;

    bool exactIndexValid = false;
    std::string safeName = StringUtils::toLower(providedName);

    // Cross-validate the index
    if (providedIndex >= 0 && providedIndex < layers.size()) {
        if (providedName.empty()) {
            exactIndexValid = true; // No name provided, trust the index
        }
        else {
            std::string actualName = StringUtils::toLower(layers[providedIndex]->name);

            if (actualName.find(safeName) != std::string::npos || safeName.find(actualName) != std::string::npos) {
                exactIndexValid = true;
            }
            else {
                printf("[AI WARNING] Index Hallucination! Index %d is '%s', not '%s'. Falling back to name search.\n",
                    providedIndex, layers[providedIndex]->name.c_str(), providedName.c_str());
            }
        }
    }

    // Route the targeting safely
    if (exactIndexValid) {
        targetIndices.push_back(providedIndex);
    }
    else if (!providedName.empty()) {
        // Fallback: Search all layers for the targetName
        for (int i = 0; i < layers.size(); ++i) {
            std::string lowerLayerName = StringUtils::toLower(layers[i]->name);
            if (lowerLayerName.find(safeName) != std::string::npos || safeName.find(lowerLayerName) != std::string::npos) {
                targetIndices.push_back(i);
            }
        }
    }

    return targetIndices;
}

// Creates a new layer and assigns the AI-provided name
void AssistantController::handleAddLayerAction(const AddLayerAction& arg) {
    m_canvas.addLayer();
    if (auto* layer = m_canvas.getActiveLayer()) {
        layer->name = arg.name;
    }
}

// Removes a layer by its exact index
void AssistantController::handleDeleteLayerAction(const DeleteLayerAction& arg) {
    m_canvas.deleteLayer(arg.targetIndex);
}

// Applies property changes (name, opacity, visibility, blend mode, lock states) to all matching layers.
// Each change that actually modifies state pushes an undo command so the user can revert it.
void AssistantController::handleModifyLayerAction(const ModifyLayerAction& arg) {
    auto& layers = m_canvas.getLayers();

    std::vector<int> targetIndices = resolveLayerIndices(arg.targetIndex, arg.targetName);
    if (targetIndices.empty()) return;

    targetIndices.reserve(layers.size());
    for (int realIndex : targetIndices) {
        bool oldVis = layers[realIndex]->visible;
        float oldOp = layers[realIndex]->opacity;
        bool oldLock = layers[realIndex]->isLocked;
        bool oldAlpha = layers[realIndex]->alphaLocked;
        bool oldClip = layers[realIndex]->isClipped;
        std::string oldName = layers[realIndex]->name;

        if (arg.newName.has_value() && arg.newName.value() != oldName) {
            m_canvas.setLayerName(realIndex, arg.newName.value());
            m_canvas.pushUndoCommand(std::make_unique<RenameLayerCommand>(realIndex, oldName, arg.newName.value()));
        }
        if (arg.newOpacity.has_value()) {
            float newOp = std::clamp(arg.newOpacity.value(), 0.0f, 1.0f);
            if (newOp != oldOp) {
                m_canvas.setLayerOpacity(realIndex, newOp);
                m_canvas.pushUndoCommand(std::make_unique<OpacityChangeCommand>(realIndex, oldOp, newOp));
            }
        }
        if (arg.newVisibility.has_value() && arg.newVisibility.value() != oldVis) {
            m_canvas.setLayerVisibility(realIndex, arg.newVisibility.value());
            m_canvas.pushUndoCommand(std::make_unique<VisibilityChangeCommand>(realIndex, oldVis, arg.newVisibility.value()));
        }
        if (arg.newBlendMode.has_value()) {
            LayerBlendMode newBlendMode = BlendModeUtils::fromString(arg.newBlendMode.value());
            int newMode = BlendModeUtils::toInt(newBlendMode);
            int oldModeVal = static_cast<int>(layers[realIndex]->blendMode);

            if (newMode != oldModeVal) {
                m_canvas.setLayerBlendMode(realIndex, newMode);
                m_canvas.pushUndoCommand(std::make_unique<BlendModeChangeCommand>(realIndex, oldModeVal, newMode));
            }
        }
        if (arg.newLock.has_value()) {
            bool newLock = arg.newLock.value();
            layers[realIndex]->isLocked = newLock;
        }

        if (arg.newAlphaLock.has_value()) {
            bool newAlphaLock = arg.newAlphaLock.value();
            layers[realIndex]->alphaLocked = newAlphaLock;
        }

        if (arg.newClipped.has_value()) {
            bool newClipped = arg.newClipped.value();
            layers[realIndex]->isClipped = newClipped;
        }
    }
}

// Reorders a layer within the stack: up/down by one slot, or top/bottom of the stack
void AssistantController::handleMoveLayerAction(const MoveLayerAction& arg) {
    auto& layers = m_canvas.getLayers();

    // Resolve and grab the first match
    auto targets = resolveLayerIndices(arg.targetIndex, arg.targetName);
    if (targets.empty()) return;

    int targetIdx = targets.front();
    int destIdx = targetIdx;
    std::string dir = StringUtils::toLower(arg.direction);

    if (dir == "up" && targetIdx < layers.size() - 1) destIdx = targetIdx + 1;
    else if (dir == "down" && targetIdx > 0) destIdx = targetIdx - 1;
    else if (dir == "top") destIdx = layers.size() - 1;
    else if (dir == "bottom") destIdx = 0;

    if (targetIdx != destIdx) m_canvas.dropLayerToReorder(targetIdx, destIdx);
}

// Creates a new folder and assigns the AI-provided name; the new folder becomes the active layer
void AssistantController::handleAddFolderAction(const AddFolderAction& arg) {
    m_canvas.addFolder();
    m_canvas.getLayers()[m_canvas.getActiveLayerIndex()]->name = arg.name;
}

// Sets the active layer by exact index or case-insensitive name match
void AssistantController::handleSelectLayerAction(const SelectLayerAction& arg) {
    int pIndex = arg.targetIndex.value_or(-1);
    std::string pName = arg.targetName.value_or("");

    auto targets = resolveLayerIndices(pIndex, pName);

    // Select the first valid layer found
    if (!targets.empty()) {
        m_canvas.setActiveLayer(targets.front());
    }
}

// Generates an image via the AI backend and applies it to the target layer.
// If the target cannot be resolved, a new layer is created automatically.
// The actual network call runs on a detached background thread; the result is queued
// in m_readyImages and consumed by processPendingActions on the main thread.
void AssistantController::handleGenerateImageAction(const GenerateImageAction& arg) {
    auto& layers = m_canvas.getLayers();

    auto targets = resolveLayerIndices(arg.targetIndex, arg.targetName);
    int targetIdx = targets.empty() ? -1 : targets.front();

    if (targetIdx == -1) {
        m_canvas.addLayer();
        targetIdx = m_canvas.getActiveLayerIndex();
    }

    layers[targetIdx]->name = "GenAI: " + arg.prompt;
    m_canvas.setActiveLayer(targetIdx);

    // Extract canvas data on the main thread
    int canvasW = m_canvas.getSize().x;
    int canvasH = m_canvas.getSize().y;

    std::thread([this, targetIdx, arg, canvasW, canvasH, aliveToken = m_isAlive]() {
        try {
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            std::string tempFile = "temp_gen_" + std::to_string(targetIdx) + "_" + std::to_string(now % 100000) + ".jpg";

#ifdef _WIN32
            // Build the dimension-aware generation payload
            nlohmann::json jPayload;
            jPayload["prompt"] = arg.prompt;
            jPayload["width"] = canvasW;
            jPayload["height"] = canvasH;
            std::string payload = jPayload.dump();

            printf("\n[GEN-AI] Starting Cloud GPU Text-to-Image...\n");
            printf("[GEN-AI] Packaged JSON Payload (%zu bytes). Connecting to Cloudflare...\n", payload.length());

            std::string apiDomain = ConfigManager::getApiDomain();
            HttpResponse response = HttpClient::post(apiDomain, "/api/generate", payload);

            if (response.success && response.statusCode == 200) {
                try {
            // Parse response and decode the returned Base64 image
                    nlohmann::json jRes = nlohmann::json::parse(response.body);
                    std::string outB64 = jRes["image"];
                    // Decode the returned Base64 image directly into RAM
                    std::vector<BYTE> binaryData = Base64Codec::decode(outB64);
                    if (binaryData.empty()) {
                        printf("[GEN-AI ERROR] Failed to decode Base64 image\n");
                        return;
                    }

					// Read the JPEG data directly from the byte array in memory without writing to disk, and queue it for the main thread
                    sf::Image generatedImg;
                    if (generatedImg.loadFromMemory(binaryData.data(), binaryData.size())) {
                        {
                            std::lock_guard<std::mutex> lock(m_imageMutex);
                            m_readyImages.push_back({ targetIdx, std::move(generatedImg) });
                        }
                        printf("\n[GEN-AI] Cloud Image successfully applied!\n");
                    }
                }
                catch (const std::exception& e) {
                    printf("\n[GEN-AI ERROR] Failed to parse JSON response: %s\n", e.what());
                }
            }
            else {
                printf("\n[GEN-AI ERROR] %s\n", response.errorMessage.c_str());
            }

            std::remove(tempFile.c_str());
#endif
        }
        catch (const std::exception& e) {
            printf("[GEN-AI ERROR] Generation thread exception: %s\n", e.what());
        }
    }).detach();
}

// Sends a source layer image to the AI for inpainting/editing, then applies the result
// to a newly created layer positioned directly above the source.
// The network call and image processing run on a detached background thread.
void AssistantController::handleEditImageAction(const EditImageAction& arg) {
    auto& layers = m_canvas.getLayers();

    int sourceIdx = m_canvas.getActiveLayerIndex();
    if (arg.sourceIndex >= 0 && arg.sourceIndex < layers.size()) {
        sourceIdx = arg.sourceIndex;
    }

    if (sourceIdx == -1 || layers.empty()) return;

    m_canvas.setActiveLayer(sourceIdx);
    m_canvas.addLayer();

    int targetIdx = m_canvas.getActiveLayerIndex();
    layers[targetIdx]->name = "AI Edit: " + arg.prompt;
    m_canvas.setActiveLayer(targetIdx);

    std::thread([this, sourceIdx, targetIdx, arg, aliveToken = m_isAlive]() {
        try {
            printf("\n[GEN-AI] Starting Cloud GPU via Cloudflare Tunnel...\n");

#ifdef _WIN32
            // Export the source layer pixels directly into RAM
            auto sourceImg = m_canvas.getLayers()[sourceIdx]->texture->getTexture().copyToImage();

            auto optionalBuffer = sourceImg.saveToMemory("png");

            if (!optionalBuffer.has_value()) {
                printf("[GEN-AI ERROR] Failed to encode source image to PNG memory!\n");
                return;
            }

            std::vector<uint8_t> imageBuffer = std::move(optionalBuffer.value());

            if (imageBuffer.empty()) {
                printf("[GEN-AI ERROR] Source image buffer is empty!\n");
                return;
            }

            // Encode the in-memory PNG to Base64
            // Convert sf::Uint8 to char so Base64Codec accepts it
            std::vector<char> charBuffer(imageBuffer.begin(), imageBuffer.end());
            std::string base64Img = Base64Codec::encode(charBuffer);

            if (base64Img.empty()) {
                printf("[GEN-AI ERROR] Failed to encode image to Base64\n");
                return;
            }

            // Build the inpainting/edit payload with strength parameter
            nlohmann::json jPayload;
            jPayload["image"] = base64Img;
            jPayload["prompt"] = arg.prompt;
            jPayload["strength"] = arg.strength.value_or(0.75f);

            std::string payload = jPayload.dump();
            printf("[GEN-AI] Packaged JSON Payload (%zu bytes). Connecting to Cloudflare...\n", payload.length());

            std::string apiDomain = ConfigManager::getApiDomain();
            HttpResponse response = HttpClient::post(apiDomain, "/api/edit", payload);

            if (response.success && response.statusCode == 200) {
                try {
                    // Parse response and decode the Base64 image back into RAM
                    nlohmann::json jRes = nlohmann::json::parse(response.body);
                    std::string outB64 = jRes["image"];
                    // Decode the returned Base64 image directly into RAM
                    std::vector<BYTE> binaryData = Base64Codec::decode(outB64);
                    if (binaryData.empty()) {
                        printf("[GEN-AI ERROR] Failed to decode Base64 image\n");
                        return;
                    }

                    sf::Image generatedImg;
                    if (generatedImg.loadFromMemory(binaryData.data(), binaryData.size())) {
                        // If the app was closed while generating, abort safely
                        if (!*aliveToken) {
                            printf("\n[GEN-AI] App closed during generation. Thread aborting safely.\n");
                            return;
                        }

                        // It is 100% safe to lock and touch 'this' now.
                        {
                            std::lock_guard<std::mutex> lock(m_imageMutex);
                            m_readyImages.push_back({ targetIdx, std::move(generatedImg) });
                        }
                        printf("\n[GEN-AI] Cloud Image Edit successfully applied!\n");
                    }
                }
                catch (const std::exception& e) {
                    printf("\n[GEN-AI ERROR] Failed to parse API JSON response: %s\n", e.what());
                }
            }
            else {
                printf("\n[GEN-AI ERROR] %s\n", response.errorMessage.c_str());
            }
#endif
        }
        catch (const std::exception& e) {
            printf("[GEN-AI ERROR] Edit thread exception: %s\n", e.what());
        }
    }).detach();
}
