#pragma once
#include "Canvas.h"
#include <memory>
#include <string>

// The generic interface for whatever AI backend you eventually plug in (OpenAI, Local LLM, etc.)
class IAssistant {
public:
    virtual ~IAssistant() = default;
    virtual void processContext(const Canvas& canvas) = 0;
};

// The bridge that translates AI decisions into standard Canvas commands
class AssistantController {
public:
    explicit AssistantController(Canvas& canvas);

    void setBackend(std::unique_ptr<IAssistant> backend) { m_backend = std::move(backend); }

    // --- HIGH-LEVEL ACTIONS (Emitted by the AI) ---
    void suggestLayer(const std::string& name);
    void applyStroke(/* parameters will go here */);
    void autoSelectRegion(/* parameters will go here */);

private:
    Canvas& m_canvas;
    std::unique_ptr<IAssistant> m_backend;
};