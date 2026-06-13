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

AssistantController::AssistantController(Canvas& canvas) : m_canvas(canvas) {}

AssistantController::~AssistantController() {
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

    // --- ADD TO CHAT HISTORY FIRST ---
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
    // std::visit unpacks the variant based on its type
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, AddLayerAction>) {
            m_canvas.addLayer();
            if (auto* layer = m_canvas.getActiveLayer()) {
                layer->name = arg.name;
            }
        }
        else if constexpr (std::is_same_v<T, DeleteLayerAction>) {
            m_canvas.deleteLayer(arg.targetIndex);
        }
        else if constexpr (std::is_same_v<T, ModifyLayerAction>) {
            auto& layers = m_canvas.getLayers();
            std::vector<int> targetIndices;

            // Try to use the exact index if the AI provided it
            if (arg.targetIndex >= 0 && arg.targetIndex < layers.size()) {
                targetIndices.push_back(arg.targetIndex);
            }
            // If no index, find ALL layers that match the name
            else if (!arg.targetName.empty()) {
                std::string safeTarget = StringUtils::toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    std::string lowerLayerName = StringUtils::toLower(layers[i]->name);
                    if (lowerLayerName.find(safeTarget) != std::string::npos ||
                        safeTarget.find(lowerLayerName) != std::string::npos) {
                        targetIndices.push_back(i);
                    }
                }
            }

            // Apply modifications to all found layers safely
            for (int realIndex : targetIndices) {
                bool oldVis = layers[realIndex]->visible;
                float oldOp = layers[realIndex]->opacity;
                int oldMode = static_cast<int>(layers[realIndex]->blendMode);
                bool oldLock = layers[realIndex]->isLocked;
                bool oldAlpha = layers[realIndex]->alphaLocked;
                bool oldClip = layers[realIndex]->isClipped;
                std::string oldName = layers[realIndex]->name;

                // Only push to the Undo stack if the value actually changed
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
                    int oldMode = static_cast<int>(layers[realIndex]->blendMode);

                    if (newMode != oldMode) {
                        m_canvas.setLayerBlendMode(realIndex, newMode);
                        m_canvas.pushUndoCommand(std::make_unique<BlendModeChangeCommand>(realIndex, oldMode, newMode));
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
        else if constexpr (std::is_same_v<T, MoveLayerAction>) {
            auto& layers = m_canvas.getLayers();
            int targetIdx = -1;

            if (arg.targetIndex >= 0 && arg.targetIndex < layers.size()) {
                targetIdx = arg.targetIndex;
            }
            else if (!arg.targetName.empty()) {
                std::string safeTarget = StringUtils::toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    if (StringUtils::toLower(layers[i]->name).find(safeTarget) != std::string::npos) {
                        targetIdx = i; break;
                    }
                }
            }

            if (targetIdx != -1) {
                int destIdx = targetIdx;
                std::string dir = StringUtils::toLower(arg.direction);

                if (dir == "up" && targetIdx < layers.size() - 1) destIdx = targetIdx + 1;
                else if (dir == "down" && targetIdx > 0) destIdx = targetIdx - 1;
                else if (dir == "top") destIdx = layers.size() - 1;
                else if (dir == "bottom") destIdx = 0;

                if (targetIdx != destIdx) m_canvas.dropLayerToReorder(targetIdx, destIdx);
            }
        }
        else if constexpr (std::is_same_v<T, AddFolderAction>) {
            m_canvas.addFolder();
            // Automatically rename the newly created folder
            m_canvas.getLayers()[m_canvas.getActiveLayerIndex()]->name = arg.name;
        }
        else if constexpr (std::is_same_v<T, SelectLayerAction>) {
            int targetIdx = -1;

            // Try to select by exact Index first
            if (arg.targetIndex.has_value()) {
                targetIdx = arg.targetIndex.value();
            }
            // Fallback to searching by Layer Name (Case-Insensitive)
            else if (arg.targetName.has_value()) {
                std::string safeTarget = StringUtils::toLower(arg.targetName.value());
                const auto& layers = m_canvas.getLayers();

                for (int i = 0; i < layers.size(); i++) {
                    if (StringUtils::toLower(layers[i]->name) == safeTarget) {
                        targetIdx = i;
                        break;
                    }
                }
            }

            // Apply the selection so the AI can jump out of folders
            if (targetIdx >= 0 && targetIdx < m_canvas.getLayers().size()) {
                m_canvas.setActiveLayer(targetIdx);
            }
        }
        else if constexpr (std::is_same_v<T, GenerateImageAction>) {
            auto& layers = m_canvas.getLayers();
            int targetIdx = -1;

            // Resolve the Target Layer
            if (arg.targetIndex >= 0 && arg.targetIndex < layers.size()) {
                targetIdx = arg.targetIndex;
            }
            else if (!arg.targetName.empty()) {
                std::string safeTarget = StringUtils::toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    if (StringUtils::toLower(layers[i]->name).find(safeTarget) != std::string::npos) {
                        targetIdx = i; break;
                    }
                }
            }

            // If no valid layer was found, create a new one
            if (targetIdx == -1) {
                m_canvas.addLayer();
                targetIdx = m_canvas.getActiveLayerIndex();
            }

            // Prepare the layer for the incoming AI Art
            layers[targetIdx]->name = "GenAI: " + arg.prompt;
            m_canvas.setActiveLayer(targetIdx);

            // Launch a detached background thread capturing 'arg' by value
            std::thread([this, targetIdx, arg]() {
                try {
                    // Add a random timestamp so threads never lock the same file
                    auto now = std::chrono::system_clock::now().time_since_epoch().count();
                    std::string tempFile = "temp_gen_" + std::to_string(targetIdx) + "_" + std::to_string(now % 100000) + ".jpg";

#ifdef _WIN32
                // Construct the Dimension-Aware Generation Payload
                nlohmann::json jPayload;
                jPayload["prompt"] = arg.prompt;
                jPayload["width"] = m_canvas.getSize().x;
                jPayload["height"] = m_canvas.getSize().y;
                std::string payload = jPayload.dump();

                printf("\n[GEN-AI] Starting Cloud GPU Text-to-Image...\n");
                printf("[GEN-AI] Packaged JSON Payload (%zu bytes). Connecting to Cloudflare...\n", payload.length());

                std::string apiDomain = ConfigManager::getApiDomain();
                HttpResponse response = HttpClient::post(apiDomain, "/api/generate", payload);

                if (response.success && response.statusCode == 200) {
                    try {
                        // Parse JSON and extract Base64
                        nlohmann::json jRes = nlohmann::json::parse(response.body);
                        std::string outB64 = jRes["image"];

                        // Decode Base64 to Binary
                        std::vector<BYTE> binaryData = Base64Codec::decode(outB64);
                        if (binaryData.empty()) {
                            printf("[GEN-AI ERROR] Failed to decode Base64 image\n");
                            std::remove(tempFile.c_str());
                            return;
                        }

                        // Save to Temp File
                        std::ofstream outFile(tempFile, std::ios::binary);
                        outFile.write(reinterpret_cast<char*>(binaryData.data()), binaryData.size());
                        outFile.close();

                        sf::Image generatedImg;
                        if (generatedImg.loadFromFile(tempFile)) {
                            {
                                std::lock_guard<std::mutex> lock(m_imageMutex);
                                m_readyImages.push_back({ targetIdx, std::move(generatedImg) });
                            }
                            printf("\n[GEN-AI] Cloud Image Generation successfully applied!\n");
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
        else if constexpr (std::is_same_v<T, EditImageAction>) {
            auto& layers = m_canvas.getLayers();

            // Resolve the Source Layer (What image is being sent to the AI)
            int sourceIdx = m_canvas.getActiveLayerIndex();
            if (arg.sourceIndex >= 0 && arg.sourceIndex < layers.size()) {
                sourceIdx = arg.sourceIndex;
            }

            if (sourceIdx == -1 || layers.empty()) return; // Nothing to edit

            m_canvas.setActiveLayer(sourceIdx); // Ensure new layer spawns directly above the source
            m_canvas.addLayer();                // Instantly creates a blank layer

            int targetIdx = m_canvas.getActiveLayerIndex();

            // Prepare the layer for the incoming AI Art
            layers[targetIdx]->name = "AI Edit: " + arg.prompt;
            m_canvas.setActiveLayer(targetIdx);

            // Launch the background network thread
            std::thread([this, sourceIdx, targetIdx, arg]() {
                try {
                    // Prove the thread started
                    printf("\n[GEN-AI] Starting Cloud GPU via Cloudflare Tunnel...\n");

                    auto now = std::chrono::system_clock::now().time_since_epoch().count();

                std::string tempSource = "temp_src_" + std::to_string(sourceIdx) + "_" + std::to_string(now % 100000) + ".png";
                std::string tempGen = "temp_gen_" + std::to_string(targetIdx) + "_" + std::to_string(now % 100000) + ".jpg";

#ifdef _WIN32
                // Extract pixels & Read binary
                auto sourceTexture = m_canvas.getLayers()[sourceIdx]->texture->getTexture();
                sourceTexture.copyToImage().saveToFile(tempSource);

                std::ifstream file(tempSource, std::ios::binary);
                std::vector<char> imageBuffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();

                if (imageBuffer.empty()) {
                    printf("[GEN-AI ERROR] Failed to read source image from canvas!\n");
                    return;
                }

                // Convert Image to Base64
                std::string base64Img = Base64Codec::encode(imageBuffer);
                if (base64Img.empty()) {
                    printf("[GEN-AI ERROR] Failed to encode image to Base64\n");
                    std::remove(tempSource.c_str());
                    std::remove(tempGen.c_str());
                    return;
                }

                // Construct the Payload
                nlohmann::json jPayload;
                jPayload["image"] = base64Img;
                jPayload["prompt"] = arg.prompt;
                jPayload["strength"] = 0.75;

                std::string payload = jPayload.dump();
                printf("[GEN-AI] Packaged JSON Payload (%zu bytes). Connecting to Cloudflare...\n", payload.length());

                std::string apiDomain = ConfigManager::getApiDomain();
                HttpResponse response = HttpClient::post(apiDomain, "/api/edit", payload);

                if (response.success && response.statusCode == 200) {
                    try {
                        // The API returns {"image": "base64..."}
                        nlohmann::json jRes = nlohmann::json::parse(response.body);
                        std::string outB64 = jRes["image"];

                        // Decode the Base64 back into raw binary bytes
                        std::vector<BYTE> binaryData = Base64Codec::decode(outB64);
                        if (binaryData.empty()) {
                            printf("[GEN-AI ERROR] Failed to decode Base64 edited image\n");
                            std::remove(tempSource.c_str());
                            std::remove(tempGen.c_str());
                            return;
                        }

                        // Save the decoded bytes to the temp file
                        std::ofstream outFile(tempGen, std::ios::binary);
                        outFile.write(reinterpret_cast<char*>(binaryData.data()), binaryData.size());
                        outFile.close();

                        sf::Image generatedImg;
                        if (generatedImg.loadFromFile(tempGen)) {
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

                std::remove(tempSource.c_str());
                std::remove(tempGen.c_str());
#endif
                }
                catch (const std::exception& e) {
                    printf("[GEN-AI ERROR] Edit thread exception: %s\n", e.what());
                }
            }).detach();
        }
    }, op);
}
