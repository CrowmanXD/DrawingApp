#pragma once
#include "AssistantController.h"
#include <string>
#include <vector>
#include <stdexcept>

// Include header-only libraries
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
    LLMAssistant(std::string host = "localhost", int port = 1234, std::string apiKey = "local-dev")
        : m_host(host), m_port(port), m_apiKey(apiKey) {
    }

    AIResponse requestAction(const AssistantContext& context, const std::vector<ChatMessage>& chatHistory) override {
        // Convert the Canvas Context into a string the AI can read
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

        // A hybrid System Prompt
        std::string systemPrompt = R"(
        You are the core routing engine for a C++ digital art application. You can chat normally, answer questions, and greet the user.
        HOWEVER, if the user asks you to modify the canvas (like adding, deleting or modifying a layer), you must include a strictly valid JSON block in your response containing the actions.
        When you answer, first write a short and friendly message to the user explaining what you did. Then, on a new line, generate exclusively the JSON block. DO NOT write anything after you close the JSON block.

        CRITICAL RULE FOR EXISTING LAYERS:
        ALWAYS use BOTH "targetIndex" and "targetName" when modifying, moving, or deleting existing layers. You MUST use the exact 'targetIndex' integer provided in the Current Canvas State. This guarantees you target the exact layer since multiple layers can share the exact same name!

        Supported Action Types:
        1. { "type": "AddLayer", "name": "Sketch" }
        2. { "type": "DeleteLayer", "targetIndex": 0, "targetName": "Background" }
        3. { "type": "ModifyLayer", "targetIndex": 3, "targetName": "My Lineart", "newOpacity": 0.5 }
        4. { "type": "ModifyLayer", "targetIndex": 1, "targetName": "Layer 1", "newName": "Base Color", "newVisibility": false }
        5. { "type": "ModifyLayer", "targetIndex": 3, "targetName": "layer 6", "newBlendMode": "Multiply" }
        6. { "type": "MoveLayer", "targetIndex": 2, "targetName": "Sketch", "direction": "up" }
        7. { "type": "ModifyLayer", "targetIndex": 4, "targetName": "Lineart", "newLock": true, "newAlphaLock": false, "newClipped": true }
        8. { "type": "AddFolder", "name": "Head" }
        9. { "type": "SelectLayer", "targetIndex": 0, "targetName": "layer 1" }
        10. { "type": "GenerateImage", "prompt": "A highly detailed watercolor painting of a futuristic city", "targetIndex": -1, "targetName": "" }
        11. { "type": "EditImage", "prompt": "make it winter", "sourceIndex": 0 }

        CRITICAL RULES FOR "EditImage" AND "GenerateImage" (PREVENT LOSSY SUMMARIZATION):
        1. Image generators do NOT understand instructions. You MUST translate the user's command into a highly descriptive visual caption.
        2. YOU MUST RE-STATE THE SUBJECT. Never use vague words like "it". If the user says "make it realistic" and the target layer is "mountainscape", you MUST explicitly combine them.
        3. ALWAYS USE THIS FORMULA: [Subject] + [User's requested style] + [Details/Lighting].
           - BAD PROMPT: "make it realistic"
           - GOOD PROMPT: "A highly detailed, realistic mountainscape, photorealistic photography"
           - BAD PROMPT: "change to winter"
           - GOOD PROMPT: "A mountainscape covered in winter snow, cold lighting, ice"
           - BAD PROMPT: "turn this sketch into a knight"
           - GOOD PROMPT: "A realistic medieval knight, heavy shiny armor, highly detailed"
        4. Provide the precise 'sourceIndex' of the layer they want to edit. If they don't specify, use the active layer index.
        5. DYNAMIC STRENGTH: You may optionally include a "strength" float between 0.1 and 1.0 for EditImage. 
           - Use 0.9 to 1.0 if the user asks to "completely change", "redraw", or drastically alter the style.
           - Use 0.75 for standard edits.
           - Use 0.3 to 0.5 if the user asks for "minor tweaks", "slight adjustments", or just color correction.
        WARNING: DO NOT use "ModifyLayer" to change the contents, pixels, or visuals of an image. It ONLY changes UI properties. Do NOT invent fields like "newPrompt".

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
        CRITICAL RULE FOR MULTIPLE ACTIONS: If you need to execute more than one action, you MUST wrap all your JSON objects inside a single JSON Array like this: [ { "type": "AddLayer" }, { "type": "ModifyLayer" } ]. Never return comma-separated objects without array brackets.
        )"
            + std::string("\nCurrent Canvas State: ") + contextJson.dump();

        // Build the message array using the full Chat History
        json messagesArray = json::array();
        messagesArray.push_back({ {"role", "system"}, {"content", systemPrompt} });

        for (const auto& msg : chatHistory) {
            std::string role = (msg.sender == "You") ? "user" : "assistant";
            messagesArray.push_back({ {"role", role}, {"content", msg.text} });
        }

        // Format the HTTP Request
        json requestBody = {
            {"model", "local-model"},
            {"messages", messagesArray},
            {"temperature", 0.2}
        };

        // Send the Request
        httplib::Client cli(m_host, m_port);
        cli.set_connection_timeout(5, 0); // 5 second timeout
        cli.set_read_timeout(120, 0);      // 120 second wait for the AI to type

        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        };

        auto res = cli.Post("/v1/chat/completions", headers, requestBody.dump(), "application/json");

        // Handle Errors
        if (!res) throw std::runtime_error("Network connection failed.");
        if (res->status != 200) throw std::runtime_error("API Error: " + std::to_string(res->status) + " - " + res->body);

        // Parse the AI's response back into C++ Actions
        AIResponse result;
        try {
            json responseJson = json::parse(res->body);

            // Check if the server response is valid
            if (!responseJson.contains("choices") || responseJson["choices"].empty()) {
                result.message = "API Error: Invalid JSON structure.";
                return result;
            }

            auto messageObj = responseJson["choices"][0]["message"];
            if (!messageObj.contains("content")) {
                result.message = "API Error: Content missing.";
                return result;
            }

            std::string rawResponse = messageObj["content"].get<std::string>();
            result.message = rawResponse;
            
			// Find the start of the JSON block (first '{' or '[')
            size_t firstBrace = rawResponse.find_first_of("{[");
            size_t lastBrace = rawResponse.find_last_of("}]");

            if (firstBrace != std::string::npos && lastBrace != std::string::npos && lastBrace >= firstBrace) {
                std::string rawJsonStr = rawResponse.substr(firstBrace, lastBrace - firstBrace + 1);

                // Auto-correct missing braces (if necessary)
                if (rawJsonStr.front() == '{' && rawJsonStr.back() == '}') {
                    rawJsonStr = "[" + rawJsonStr + "]";
                }

                // Parse the JSON. If it fails, it will jump to catch
                json parsedData = json::parse(rawJsonStr);

                auto extractAction = [&](const json& actionObj) {
                    if (!actionObj.is_object() || !actionObj.contains("type")) return;
                    std::string type = actionObj["type"].get<std::string>();

                    if (type == "AddLayer" && actionObj.contains("name")) {
                        result.actions.push_back(AddLayerAction{ actionObj["name"].get<std::string>() });
                    }
                    else if (type == "DeleteLayer" && actionObj.contains("targetIndex")) {
                        result.actions.push_back(DeleteLayerAction{ actionObj["targetIndex"].get<int>() });
                    }
                    else if (type == "ModifyLayer") {
                        ModifyLayerAction mod;
                        if (actionObj.contains("targetName")) mod.targetName = actionObj["targetName"].get<std::string>();
                        if (actionObj.contains("targetIndex")) mod.targetIndex = actionObj["targetIndex"].get<int>();
                        if (actionObj.contains("newName")) mod.newName = actionObj["newName"].get<std::string>();
                        if (actionObj.contains("newVisibility")) mod.newVisibility = actionObj["newVisibility"].get<bool>();
                        if (actionObj.contains("newLock")) mod.newLock = actionObj["newLock"].get<bool>();
                        if (actionObj.contains("newAlphaLock")) mod.newAlphaLock = actionObj["newAlphaLock"].get<bool>();
                        if (actionObj.contains("newClipped")) mod.newClipped = actionObj["newClipped"].get<bool>();

                        if (actionObj.contains("newOpacity")) {
                            // nlohmann::json .get<float>() automatically converts int and float
                            mod.newOpacity = actionObj["newOpacity"].get<float>();
                        }

                        if (actionObj.contains("newBlendMode")) mod.newBlendMode = actionObj["newBlendMode"].get<std::string>();
                        else if (actionObj.contains("newBlendingMode")) mod.newBlendMode = actionObj["newBlendingMode"].get<std::string>();

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
                        result.actions.push_back(AddFolderAction{ actionObj["name"].get<std::string>() });
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
                        if (actionObj.contains("strength")) edit.strength = actionObj["strength"].get<float>();
                        result.actions.push_back(edit);
                    }
                    };

                if (parsedData.is_array()) {
                    for (const auto& action : parsedData) extractAction(action);
                }
                else if (parsedData.is_object()) {
                    extractAction(parsedData);
                }
            }
        }
        catch (const std::exception& e) {
            printf("[JSON ERROR] Parsing failed: %s\n", e.what());
        }

        return result;
    }

private:
    std::string m_host;
    int m_port;
    std::string m_apiKey;
};