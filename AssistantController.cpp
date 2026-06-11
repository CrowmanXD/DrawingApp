#include "AssistantController.h"
#include "LayerUndoCommands.h"
#include "StrokeUndoCommand.h"
#include <fstream>
#include <cstdlib> // For rand()
#include <cstdio> // For std::remove
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

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
                    targetLayer->texture->draw(sf::Sprite(tex), sf::RenderStates(sf::BlendNone));
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

            auto toLower = [](std::string s) {
                for (char& c : s) c = std::tolower(c);
                return s;
                };

            // Try to use the exact index if the AI provided it
            if (arg.targetIndex >= 0 && arg.targetIndex < layers.size()) {
                targetIndices.push_back(arg.targetIndex);
            }
            // If no index, find ALL layers that match the name
            else if (!arg.targetName.empty()) {
                std::string safeTarget = toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    if (toLower(layers[i]->name).find(safeTarget) != std::string::npos ||
                        safeTarget.find(toLower(layers[i]->name)) != std::string::npos) {

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
                    std::string modeStr = toLower(arg.newBlendMode.value());
                    int newMode = 0;
                    if (modeStr.find("multiply") != std::string::npos) newMode = 1;
                    else if (modeStr.find("add") != std::string::npos) newMode = 2;
                    else if (modeStr.find("passthrough") != std::string::npos) newMode = 3;

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
                auto toLower = [](std::string s) {
                    for (char& c : s) c = std::tolower(c); return s;
                    };
                std::string safeTarget = toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    if (toLower(layers[i]->name).find(safeTarget) != std::string::npos) {
                        targetIdx = i; break;
                    }
                }
            }

            if (targetIdx != -1) {
                int destIdx = targetIdx;
                std::string dir = arg.direction;
                for (char& c : dir) c = std::tolower(c);

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
                auto toLower = [](std::string s) {
                    for (char& c : s) c = std::tolower(c);
                    return s;
                    };

                std::string safeTarget = toLower(arg.targetName.value());
                const auto& layers = m_canvas.getLayers();

                for (int i = 0; i < layers.size(); i++) {
                    if (toLower(layers[i]->name) == safeTarget) {
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
                auto toLower = [](std::string s) {
                    for (char& c : s) c = std::tolower(c); return s;
                    };
                std::string safeTarget = toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    if (toLower(layers[i]->name).find(safeTarget) != std::string::npos) {
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
                std::string tempFile = "temp_gen_" + std::to_string(targetIdx) + ".jpg";

#ifdef _WIN32
                std::remove(tempFile.c_str());

                // 1. Sanitize the prompt for the JSON Payload
                std::string safePrompt = arg.prompt;
                size_t pos = 0;
                while ((pos = safePrompt.find('"', pos)) != std::string::npos) { safePrompt.replace(pos, 1, "\\\""); pos += 2; }
                pos = 0;
                while ((pos = safePrompt.find('\n', pos)) != std::string::npos) { safePrompt.replace(pos, 1, " "); pos += 1; }

                std::string payload = "{\"inputs\": \"" + safePrompt + "\"}";

                // 2. Setup the Native Windows HTTP Request
                std::string hfToken = "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"; // Generate a new one on Hugging Face!
                std::string headers = "Authorization: Bearer " + hfToken + "\r\n"
                    "Content-Type: application/json\r\n"
                    "Accept: image/jpeg\r\n";

                // Open Native Internet Session
                HINTERNET hSession = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
                if (hSession) {
                    // Connect directly to the API
                    HINTERNET hConnect = InternetConnectA(hSession, "api-inference.huggingface.co", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
                    if (!hConnect)
                    {
                        printf("InternetConnectA failed: %lu\n", GetLastError());
                    }
                    else
                    {
                        // Open the specific model endpoint
                        HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/models/black-forest-labs/FLUX.1-schnell", NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
                        if (!hRequest)
                        {
                            printf("HttpOpenRequestA failed: %lu\n", GetLastError());
                        }
                        else
                        {
                            // 3. Send the HTTP POST Request entirely in C++ Memory!
                            if (!HttpSendRequestA(hRequest, headers.c_str(), headers.length(), (LPVOID)payload.c_str(), payload.length())) 
                            {
                                printf("HttpSendRequestA failed: %lu\n", GetLastError());
                            }
                            else
                            {
                                DWORD statusCode = 0;
                                DWORD length = sizeof(DWORD);
                                HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &length, NULL);

                                if (statusCode == 200) {
                                    // 4. It succeeded! Write the raw bytes to our JPEG file
                                    std::ofstream outFile(tempFile, std::ios::binary);
                                    char buffer[8192];
                                    DWORD bytesRead = 0;
                                    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                                        outFile.write(buffer, bytesRead);
                                    }
                                    outFile.close();

                                    // Load the real image onto the Canvas
                                    sf::Image generatedImg;
                                    if (generatedImg.loadFromFile(tempFile)) {
                                        std::lock_guard<std::mutex> lock(m_imageMutex);
                                        m_readyImages.push_back({ targetIdx, generatedImg });
                                        printf("\n[GEN-AI] Image successfully downloaded via Native WinINet!\n");
                                    }
                                }
                                else {
                                    // Server returned an error (e.g., 503 Model is Loading)
                                    char buffer[8192];
                                    DWORD bytesRead = 0;
                                    std::string errText;
                                    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                                        errText.append(buffer, bytesRead);
                                    }
                                    printf("\n[GEN-AI ERROR] Hugging Face returned Status %lu:\n%s\n\n", statusCode, errText.c_str());
                                }
                            }
                            else {
                                printf("\n[GEN-AI ERROR] Native HTTP Request failed entirely. Code: %lu\n\n", GetLastError());
                            }
                            InternetCloseHandle(hRequest);
                        }
                        InternetCloseHandle(hConnect);
                    }
                    InternetCloseHandle(hSession);
                }
#endif
                }).detach();
            }
        }, op);
}