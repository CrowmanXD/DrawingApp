#pragma once
#include "AssistantController.h"
#include <string>
#include <vector>
#include <stdexcept>

// Include our new header-only libraries
#include "httplib.h"
#include "json.hpp"

// Force MSVC to link the Windows Networking library automatically
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

using json = nlohmann::json;

class LLMAssistant : public IAssistant {
public:
    // By default, this points to a local AI server (like LMStudio or Ollama). 
    // You can change this to "api.openai.com" and provide a real API key later
    LLMAssistant(std::string host = "localhost", int port = 1234, std::string apiKey = "local-dev")
        : m_host(host), m_port(port), m_apiKey(apiKey) {
    }

    AIResponse requestAction(const AssistantContext& context, const std::vector<ChatMessage>& chatHistory) override {
        // 1. Convert the Canvas Context into a string the AI can read
        json contextJson;
        contextJson["canvas_width"] = context.canvasSize.x;
        contextJson["canvas_height"] = context.canvasSize.y;
        contextJson["active_layer_index"] = context.activeLayerIndex;

        json layersArray = json::array();
        int internalIndex = 0;
        for (const auto& layer : context.layers) {
            layersArray.push_back({ {"targetIndex", internalIndex}, {"name", layer.name}, {"visible", layer.visible} });
            internalIndex++;
        }
        contextJson["layers"] = layersArray;

        // 2. A hybrid System Prompt
        std::string systemPrompt =
            "You are a helpful AI co-pilot in a digital drawing app. "
            "You can chat normally, answer questions, and greet the user. "
            "HOWEVER, if the user asks you to modify the canvas (like adding, deleting or modifying a layer), "
            "you must include a JSON block in your response containing the actions. \n"
            "Note: ALWAYS use the exact 'targetIndex' integer provided in the Current Canvas State when modifying layers, even if the layer's name contains a different number.\n"
            "Supported Action Types:\n"
            "1. { \"type\": \"AddLayer\", \"name\": \"Sketch\" }\n"
            "2. { \"type\": \"DeleteLayer\", \"targetIndex\": 0 }\n"
            "3. { \"type\": \"ModifyLayer\", \"targetName\": \"My Lineart\", \"newOpacity\": 0.5 }\n"
            "4. { \"type\": \"ModifyLayer\", \"targetName\": \"Background\", \"newVisibility\": false }\n"
            "5. { \"type\": \"ModifyLayer\", \"targetName\": \"Layer 1\", \"newName\": \"Base Color\" }\n"
            "Note: For ModifyLayer, ALWAYS use 'targetName' to specify which layer to change. Only include the specific properties you want to change. Do NOT include 'newName' or 'newVisibility' unless specifically requested.\n"
            "Format your actions exactly like this: \n"
            "```json\n{ \"actions\": [ { \"type\": \"AddLayer\", \"name\": \"Sketch\" } ] }\n```\n"
            "Current Canvas State: " + contextJson.dump();

        // 3. Build the message array using the full Chat History
        json messagesArray = json::array();
        messagesArray.push_back({ {"role", "system"}, {"content", systemPrompt} });

        for (const auto& msg : chatHistory) {
            std::string role = (msg.sender == "You") ? "user" : "assistant";
            messagesArray.push_back({ {"role", role}, {"content", msg.text} });
        }

        // 4. Format the HTTP Request
        json requestBody = {
            {"model", "local-model"},
            {"messages", messagesArray},
            {"temperature", 0.4} // Slightly higher temperature allows for more natural conversation
        };

        // 5. Send the Request
        httplib::Client cli(m_host, m_port);
        cli.set_connection_timeout(5, 0); // 5 second timeout
        cli.set_read_timeout(15, 0);      // 15 second wait for the AI to type

        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        };

        auto res = cli.Post("/v1/chat/completions", headers, requestBody.dump(), "application/json");

        // 6. Handle Errors
        if (!res) throw std::runtime_error("Network connection failed.");
        if (res->status != 200) throw std::runtime_error("API Error: " + std::to_string(res->status) + " - " + res->body);

        // 7. Parse the AI's response back into C++ Actions
        AIResponse result;
        try {
            json responseJson = json::parse(res->body);
            result.message = responseJson["choices"][0]["message"]["content"];

            // Find the actual start and end of the JSON object to ignore conversational text
            size_t firstBrace = result.message.find_first_of("{[");
            size_t lastBrace = result.message.find_last_of("}]");

            if (firstBrace != std::string::npos && lastBrace != std::string::npos && lastBrace >= firstBrace) {
                std::string rawJsonStr = result.message.substr(firstBrace, lastBrace - firstBrace + 1);
                json parsedData = json::parse(rawJsonStr);

                // Helper lambda to safely extract a single action
                auto extractAction = [&](const json& actionObj) {
                    if (!actionObj.contains("type")) return;
                    std::string type = actionObj["type"];
                    if (type == "AddLayer" && actionObj.contains("name")) {
                        result.actions.push_back(AddLayerAction{ actionObj["name"] });
                    }
                    else if (type == "DeleteLayer" && actionObj.contains("targetIndex")) {
                        result.actions.push_back(DeleteLayerAction{ actionObj["targetIndex"] });
                    }
                    else if (type == "ModifyLayer") {
                        ModifyLayerAction mod;

                        // Explicitly tell the JSON parser what C++ types to extract
                        if (actionObj.contains("targetName")) mod.targetName = actionObj["targetName"].get<std::string>();
                        if (actionObj.contains("targetIndex")) mod.targetIndex = actionObj["targetIndex"].get<int>();

                        if (actionObj.contains("newName")) mod.newName = actionObj["newName"].get<std::string>();
                        if (actionObj.contains("newVisibility")) mod.newVisibility = actionObj["newVisibility"].get<bool>();

                        // Opacity might be parsed as an int if it's "1" or "0", so we read it as a float safely
                        if (actionObj.contains("newOpacity")) {
                            mod.newOpacity = actionObj["newOpacity"].is_number_float() ?
                                actionObj["newOpacity"].get<float>() :
                                (float)actionObj["newOpacity"].get<int>();
                        }

                        result.actions.push_back(mod);
                    }
                    };

                // Did the AI send an array of actions, or just one raw object?
                if (parsedData.contains("actions") && parsedData["actions"].is_array()) {
                    for (const auto& action : parsedData["actions"]) extractAction(action);
                }
                else if (parsedData.is_array()) {
                    for (const auto& action : parsedData) extractAction(action);
                }
                else if (parsedData.is_object()) {
                    extractAction(parsedData);
                }
            }
        }
        catch (...) {
            // If parsing fails, we don't throw an error, we just return the text message natively.
        }

        return result;
    }

private:
    std::string m_host;
    int m_port;
    std::string m_apiKey;
};