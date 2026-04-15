#pragma once
#include "Canvas.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <variant>

// --- GAP 6: ERROR & STATE REPORTING ---
enum class AssistantState {
    Idle,
    Thinking,
    Applying,
    Error
};

// --- GAP 3: TYPED ACTION CONTRACT ---
// These DTOs contain pure data. No rendering logic allowed here.
struct AddLayerAction { std::string name; };
struct DeleteLayerAction { int targetIndex; };
struct StrokeAction { std::vector<sf::Vector2f> points; float size; sf::Color color; };

// A strict variant holding only approved operations
using AIOperation = std::variant<AddLayerAction, DeleteLayerAction, StrokeAction>;

class IAssistant {
public:
    virtual ~IAssistant() = default;
    // Returns a list of strictly typed actions, or throws an exception on network failure
    virtual std::vector<AIOperation> requestAction(const Canvas& currentContext, const std::string& userPrompt) = 0;
};

// --- GAP 4: ASYNC EXECUTION MODEL ---
class AssistantController {
public:
    explicit AssistantController(Canvas& canvas);
    ~AssistantController();

    void setBackend(std::unique_ptr<IAssistant> backend) { m_backend = std::move(backend); }

    // Thread-safe state access for ImGui
    AssistantState getState() const;
    std::string getLastError() const;

    // Kicks off the background thread
    void requestAIHelp(const std::string& prompt);

    // Called every frame by Application::update() to safely process finished AI tasks on the MAIN thread
    void processPendingActions(bool previewOnly); // Gap 5: Enforce preview boundary here

private:
    Canvas& m_canvas;
    std::unique_ptr<IAssistant> m_backend;

    // Async machinery
    std::thread m_workerThread;
    std::atomic<AssistantState> m_state{ AssistantState::Idle };

    mutable std::mutex m_mutex;
    std::string m_lastError;
    std::vector<AIOperation> m_pendingOperations;

    void executeAction(const AIOperation& op, bool previewOnly);
};