#include "AssistantController.h"

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

void AssistantController::requestAIHelp(const std::string& prompt) {
    if (m_state.load() == AssistantState::Thinking) return; // Prevent spamming
    if (!m_backend) return;

    // 1. Lock state to Thinking
    m_state.store(AssistantState::Thinking);

    // 2. Clear old data
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
        m_pendingOperations.clear();
    }

    // 3. Clean up the old thread if it exists
    if (m_workerThread.joinable()) m_workerThread.join();

    // 4. Spin up the background worker
    m_workerThread = std::thread([this, prompt]() {
        try {
            // This blocks the background thread, NOT the UI!
            auto results = m_backend->requestAction(m_canvas, prompt);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingOperations = std::move(results);
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

    // --- GAP 5: ENFORCED MUTATION BOUNDARY ---
    if (!m_pendingOperations.empty()) {
        m_canvas.beginBatchCommand();

        for (const auto& op : m_pendingOperations) {
            executeAction(op, previewOnly);
        }

        m_canvas.endBatchCommand();
        m_pendingOperations.clear();
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
        else if constexpr (std::is_same_v<T, StrokeAction>) {
            // TODO: Route through standard brush interpolation math
        }
        }, op);
}