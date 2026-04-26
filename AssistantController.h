#pragma once
#include "Canvas.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <variant>
#include <optional>

struct AssistantLayerInfo {
    std::string name;
    bool visible = true;
};

struct AssistantContext {
    sf::Vector2u canvasSize{ 0, 0 };
    int activeLayerIndex = -1;
    bool hasSelection = false;
    std::vector<AssistantLayerInfo> layers;
};

// --- ERROR & STATE REPORTING ---
enum class AssistantState {
    Idle,
    Thinking,
    Applying,
    Error
};

// --- TYPED ACTION CONTRACT ---
// These DTOs contain pure data. No rendering logic allowed here.
struct AddLayerAction { std::string name; };
struct DeleteLayerAction { int targetIndex; };
struct StrokeAction { std::vector<sf::Vector2f> points; float size; sf::Color color; };
struct ModifyLayerAction {
    int targetIndex = -1; // Default to -1 so we know if the AI didn't provide it
    std::string targetName = "";
    std::optional<std::string> newName;
    std::optional<bool> newVisibility;
    std::optional<float> newOpacity;
};

// A strict variant holding only approved operations
using AIOperation = std::variant<AddLayerAction, DeleteLayerAction, StrokeAction, ModifyLayerAction>;

struct AIResponse {
    std::string message;
    std::vector<AIOperation> actions;
};

struct ChatMessage {
    std::string sender; // "You" or "AI"
    std::string text;
};

class IAssistant {
public:
    virtual ~IAssistant() = default;
    virtual AIResponse requestAction(const AssistantContext& currentContext, const std::vector<ChatMessage>& chatHistory) = 0;
};

// --- ASYNC EXECUTION MODEL ---
class AssistantController {
public:
    explicit AssistantController(Canvas& canvas);
    ~AssistantController();

    void setBackend(std::unique_ptr<IAssistant> backend) { m_backend = std::move(backend); }

    // Thread-safe state access for ImGui
    AssistantState getState() const;
    std::string getLastError() const;
    const std::vector<ChatMessage>& getChatHistory() const { return m_chatHistory; }

    // Kicks off the background thread
    void requestAIHelp(const std::string& prompt);

    // Called every frame by Application::update() to safely process finished AI tasks on the MAIN thread
    void processPendingActions(bool previewOnly); // Enforce preview boundary here

private:
    Canvas& m_canvas;
    std::unique_ptr<IAssistant> m_backend;

    // Async machinery
    std::thread m_workerThread;
    std::atomic<AssistantState> m_state{ AssistantState::Idle };

    mutable std::mutex m_mutex;
    std::string m_lastError;
    AIResponse m_pendingResponse;
    std::vector<ChatMessage> m_chatHistory;

    AssistantContext buildContextSnapshot() const;
    void executeAction(const AIOperation& op, bool previewOnly);
};