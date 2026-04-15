#pragma once
#include "AssistantController.h"
#include <thread>
#include <chrono>

class MockAssistant : public IAssistant {
public:
    std::vector<AIOperation> requestAction(const Canvas& currentContext, const std::string& userPrompt) override {
        // 1. Simulate network latency (fake 2-second delay)
        // Because this runs on our background thread, your UI will NOT freeze during this time!
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // 2. Return a hardcoded action as if the AI decided to do this
        std::vector<AIOperation> actions;
        actions.push_back(AddLayerAction{ "AI Sketch Layer" });

        return actions;
    }
};