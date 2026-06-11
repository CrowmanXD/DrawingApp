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
    bool isLocked;
    bool alphaLocked;
    bool isClipped;
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
struct ModifyLayerAction {
    int targetIndex = -1; // Default to -1 to know if the AI didn't provide it
    std::string targetName = "";
    std::optional<std::string> newName;
    std::optional<bool> newVisibility;
    std::optional<float> newOpacity;
    std::optional<std::string> newBlendMode;
    std::optional<bool> newLock;
    std::optional<bool> newAlphaLock;
    std::optional<bool> newClipped;
};

struct MoveLayerAction {
    int targetIndex = -1;
    std::string targetName = "";
    std::string direction = "up"; // "up", "down", "top", or "bottom"
};

struct AddFolderAction {
    std::string name;
};

struct SelectLayerAction {
    std::optional<std::string> targetName;
    std::optional<int> targetIndex;
};

struct GenerateImageAction {
    std::string prompt;
    int targetIndex = -1;
    std::string targetName = "";
};

// A strict variant holding only approved operations
using AIOperation = std::variant<AddLayerAction, DeleteLayerAction, ModifyLayerAction, MoveLayerAction, AddFolderAction, SelectLayerAction, GenerateImageAction>;

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

    // Starts the background thread
    void requestAIHelp(const std::string& prompt);

    // Called every frame by Application::update() to safely process finished AI tasks on the MAIN thread
    void processPendingActions();

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
    std::vector<std::pair<int, sf::Image>> m_readyImages;
    std::mutex m_imageMutex;

    AssistantContext buildContextSnapshot() const;
    void executeAction(const AIOperation& op);
};