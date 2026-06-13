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
        std::string systemPrompt = R"(
        You are the core routing engine for a C++ digital art application. You can chat normally, answer questions, and greet the user.
        HOWEVER, if the user asks you to modify the canvas (like adding, deleting or modifying a layer), you must include a strictly valid JSON block in your response containing the actions.

        CRITICAL RULE FOR EXISTING LAYERS:
        ALWAYS use BOTH "targetIndex" and "targetName" when modifying, moving, or deleting existing layers. You MUST use the exact 'targetIndex' integer provided in the Current Canvas State. This guarantees you target the exact layer since multiple layers can share the exact same name!

        Supported Action Types:
        1. { "type": "AddLayer", "name": "Sketch" }
        2. { "type": "DeleteLayer", "targetIndex": 0, "targetName": "Background" }
        3. { "type": "ModifyLayer", "targetIndex": 3, "targetName": "My Lineart", "newOpacity": 0.5 }
        4. { "type": "ModifyLayer", "targetIndex": 1, "targetName": "Layer 1", "newName": "Base Color", "newVisibility": false }
        5. { "type": "MoveLayer", "targetIndex": 2, "targetName": "Sketch", "direction": "up" }
        6. { "type": "ModifyLayer", "targetIndex": 4, "targetName": "Lineart", "newLock": true, "newAlphaLock": false, "newClipped": true }
        7. { "type": "AddFolder", "name": "Head" }
        8. { "type": "SelectLayer", "targetIndex": 0, "targetName": "layer 1" }
        9. { "type": "GenerateImage", "prompt": "A highly detailed watercolor painting of a futuristic city", "targetIndex": -1, "targetName": "" }
        10. { "type": "EditImage", "prompt": "make it winter", "sourceIndex": 0 }

        CRITICAL RULES FOR "EditImage" AND "GenerateImage" (PREVENT LOSSY SUMMARIZATION):
        1. DO NOT SUMMARIZE. The 'prompt' field is piped directly into a Stable Diffusion engine. 
        2. You MUST retain all visual subjects, styles, lighting, and descriptive keywords.
        3. TRANSLATE COMMANDS INTO DESCRIPTIONS. Image generators do not understand instructions. 
           If the user says: "make the mountainscape nighttime"
           You MUST rewrite the prompt as: "A mountainscape at night, dark sky, moonlight"
           If the user says: "turn this sketch into a realistic knight"
           You MUST rewrite the prompt as: "A realistic photo of a medieval knight, highly detailed"
        4. Provide the precise 'sourceIndex' of the layer they want to edit. If they don't specify, use the active layer index.

        CRITICAL RULES FOR HIERARCHY AND NESTING:
        - Adding a layer or a folder automatically selects it.
        - To put layers INSIDE a folder, you must execute "AddLayer" operations immediately after an "AddFolder" operation.
        - To create a new ROOT folder or a standalone layer afterwards, you MUST use a "SelectLayer" operation to explicitly click back onto the bottom-most root layer BEFORE creating the next folder. Otherwise, folders will nest inside each other forever!

        EXAMPLE OF A PERFECT CHARACTER RIG SEQUENCE:
        1. { "type": "AddFolder", "name": "Head" }
        2. { "type": "AddLayer", "name": "Lineart" }
        3. { "type": "SelectLayer", "targetIndex": 0, "targetName": "layer 1" }
        4. { "type": "AddFolder", "name": "Body" }

        Note: For MoveLayer, 'direction' must be 'up', 'down', 'top', or 'bottom'.
        CRITICAL: DO NOT use comments (like //) inside the JSON block. It must be strictly valid JSON.
        Format your actions exactly wrapped inside a standard json markdown code block.
        )"
            + std::string("\nCurrent Canvas State: ") + contextJson.dump();

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
            {"temperature", 0.2}
        };

        // 5. Send the Request
        httplib::Client cli(m_host, m_port);
        cli.set_connection_timeout(5, 0); // 5 second timeout
        cli.set_read_timeout(120, 0);      // 120 second wait for the AI to type

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
                        if (actionObj.contains("newLock")) mod.newLock = actionObj["newLock"].get<bool>();
                        if (actionObj.contains("newAlphaLock")) mod.newAlphaLock = actionObj["newAlphaLock"].get<bool>();
                        if (actionObj.contains("newClipped")) mod.newClipped = actionObj["newClipped"].get<bool>();

                        // Opacity might be parsed as an int if it's "1" or "0", so read it as a float
                        if (actionObj.contains("newOpacity")) {
                            mod.newOpacity = actionObj["newOpacity"].is_number_float() ?
                                actionObj["newOpacity"].get<float>() :
                                (float)actionObj["newOpacity"].get<int>();
                        }
                        if (actionObj.contains("newBlendMode")) mod.newBlendMode = actionObj["newBlendMode"].get<std::string>();

                        result.actions.push_back(mod);
                    }
                    else if (type == "MoveLayer") {
                        MoveLayerAction move;
                        if (actionObj.contains("targetIndex")) move.targetIndex = actionObj["targetIndex"].get<int>();
                        if (actionObj.contains("targetName")) move.targetName = actionObj["targetName"].get<std::string>();
                        if (actionObj.contains("direction")) move.direction = actionObj["direction"].get<std::string>();
                        result.actions.push_back(move);
                    }
                    else if (type == "AddFolder" && actionObj.contains("name")) {
                        result.actions.push_back(AddFolderAction{ actionObj["name"] });
                    }
                    else if (type == "SelectLayer") {
                        SelectLayerAction sel;
                        if (actionObj.contains("targetName")) sel.targetName = actionObj["targetName"].get<std::string>();
                        if (actionObj.contains("targetIndex")) sel.targetIndex = actionObj["targetIndex"].get<int>();
                        result.actions.push_back(sel);
                    }
                    else if (type == "GenerateImage" && actionObj.contains("prompt")) {
                        GenerateImageAction gen;
                        gen.prompt = actionObj["prompt"].get<std::string>();
                        if (actionObj.contains("targetIndex")) gen.targetIndex = actionObj["targetIndex"].get<int>();
                        if (actionObj.contains("targetName")) gen.targetName = actionObj["targetName"].get<std::string>();
                        result.actions.push_back(gen);
                    }
                    else if (type == "EditImage" && actionObj.contains("prompt")) {
                        EditImageAction edit;
                        edit.prompt = actionObj["prompt"].get<std::string>();
                        if (actionObj.contains("sourceIndex")) edit.sourceIndex = actionObj["sourceIndex"].get<int>();
                        if (actionObj.contains("targetIndex")) edit.targetIndex = actionObj["targetIndex"].get<int>();
                        if (actionObj.contains("targetName")) edit.targetName = actionObj["targetName"].get<std::string>();
                        result.actions.push_back(edit);
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