#pragma once
#include "AssistantController.h"
#include <thread>
#include <chrono>

class MockAssistant : public IAssistant {
public:
    AIResponse requestAction(const AssistantContext& currentContext, const std::vector<ChatMessage>& chatHistory) override {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        AIResponse response;
        response.message = "I've created the new layer you asked for!";

        const std::string prefix = chatHistory.empty() ? "AI Sketch" : "AI: " + chatHistory.back().text.substr(0, 10);
        response.actions.push_back(AddLayerAction{ prefix });

        return response;
    }
};