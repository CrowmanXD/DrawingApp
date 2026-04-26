#include "AssistantController.h"
#include "LayerUndoCommands.h"

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
            layer->visible
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

void AssistantController::processPendingActions(bool previewOnly) {
    if (m_state.load() != AssistantState::Applying) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // --- LOG AI RESPONSE AND EXECUTE ACTIONS ---
    m_chatHistory.push_back({ "AI", m_pendingResponse.message });

    if (!m_pendingResponse.actions.empty()) {
        m_canvas.beginBatchCommand();
        for (const auto& op : m_pendingResponse.actions) executeAction(op, previewOnly);
        m_canvas.endBatchCommand();
    }

    m_state.store(AssistantState::Idle);
}

void AssistantController::executeAction(const AIOperation& op, bool previewOnly) {
    // std::visit unpacks the variant based on its type
    std::visit([this, previewOnly](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, AddLayerAction>) {
            m_canvas.addLayer();
            if (auto* layer = m_canvas.getActiveLayer()) {
                layer->name = arg.name;
                if (previewOnly) layer->opacity = 0.5f; // Visual indicator of a preview layer
            }
        }
        else if constexpr (std::is_same_v<T, DeleteLayerAction>) {
            if (!previewOnly) {
                m_canvas.deleteLayer(arg.targetIndex);
            }
        }
        else if constexpr (std::is_same_v<T, ModifyLayerAction>) {
            auto& layers = m_canvas.getLayers();
            int realIndex = arg.targetIndex;

            auto toLower = [](std::string s) {
                for (char& c : s) c = std::tolower(c);
                return s;
                };

            if (realIndex < 0 || realIndex >= layers.size()) {
                std::string safeTarget = toLower(arg.targetName);
                for (int i = 0; i < layers.size(); ++i) {
                    if (toLower(layers[i]->name).find(safeTarget) != std::string::npos ||
                        safeTarget.find(toLower(layers[i]->name)) != std::string::npos) {
                        realIndex = i;
                        break;
                    }
                }
            }

            if (realIndex >= 0 && realIndex < layers.size()) {
                // 1. Capture the "Old" state
                std::string oldName = layers[realIndex]->name;
                float oldOpacity = layers[realIndex]->opacity;
                bool oldVisibility = layers[realIndex]->visible;

                // 2. Safely apply changes and push individual atomic commands
                if (arg.newName.has_value()) {
                    std::string newName = arg.newName.value();
                    m_canvas.setLayerName(realIndex, newName);
                    m_canvas.pushUndoCommand(std::make_unique<RenameLayerCommand>(realIndex, oldName, newName));
                }

                if (arg.newOpacity.has_value()) {
                    float newOpacity = std::clamp(arg.newOpacity.value(), 0.0f, 1.0f);
                    m_canvas.setLayerOpacity(realIndex, newOpacity);
                    m_canvas.pushUndoCommand(std::make_unique<OpacityChangeCommand>(realIndex, oldOpacity, newOpacity));
                }

                if (arg.newVisibility.has_value()) {
                    bool newVis = arg.newVisibility.value();
                    m_canvas.setLayerVisibility(realIndex, newVis);
                    m_canvas.pushUndoCommand(std::make_unique<VisibilityChangeCommand>(realIndex, oldVisibility, newVis));
                }
            }
        }
        else if constexpr (std::is_same_v<T, StrokeAction>) {
            // TODO: Route through standard brush interpolation math
        }
        }, op);
}