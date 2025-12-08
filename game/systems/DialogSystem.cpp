#include "DialogSystem.hpp"
#include "../../engine/core/Logger.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace CatGame {

DialogSystem::DialogSystem(int priority)
    : System(priority)
{}

void DialogSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);
    CatEngine::Logger::info("DialogSystem initialized");
}

void DialogSystem::update(float dt) {
    // Dialog system is mainly event-driven
    // Update logic can be added here if needed
}

bool DialogSystem::loadDialogTree(const std::string& filepath) {
    DialogTree tree;
    if (!parseDialogTree(filepath, tree)) {
        return false;
    }

    if (!validateDialogTree(tree)) {
        CatEngine::Logger::error("Invalid dialog tree: " + tree.id);
        return false;
    }

    dialogTrees_[tree.id] = tree;
    CatEngine::Logger::info("Loaded dialog tree: " + tree.id + " from " + filepath);
    return true;
}

bool DialogSystem::loadDialogTreesFromDirectory(const std::string& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        CatEngine::Logger::error("Dialog directory not found: " + directory);
        return false;
    }

    int loadedCount = 0;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            if (loadDialogTree(entry.path().string())) {
                loadedCount++;
            }
        }
    }

    CatEngine::Logger::info("Loaded " + std::to_string(loadedCount) + " dialog trees from " + directory);
    return loadedCount > 0;
}

const DialogTree* DialogSystem::getDialogTree(const std::string& treeId) const {
    auto it = dialogTrees_.find(treeId);
    return (it != dialogTrees_.end()) ? &it->second : nullptr;
}

bool DialogSystem::hasDialogTree(const std::string& treeId) const {
    return dialogTrees_.find(treeId) != dialogTrees_.end();
}

void DialogSystem::startDialog(const std::string& treeId) {
    const DialogTree* tree = getDialogTree(treeId);
    if (!tree) {
        CatEngine::Logger::error("Dialog tree not found: " + treeId);
        return;
    }

    if (tree->nodes.find(tree->startNodeId) == tree->nodes.end()) {
        CatEngine::Logger::error("Start node not found in dialog tree: " + treeId);
        return;
    }

    inDialog_ = true;
    currentTreeId_ = treeId;
    currentNodeId_ = tree->startNodeId;

    if (onDialogStart_) {
        onDialogStart_(treeId);
    }

    if (onNodeChange_) {
        onNodeChange_(currentNodeId_);
    }

    CatEngine::Logger::info("Started dialog: " + treeId);
}

void DialogSystem::endDialog() {
    if (!inDialog_) {
        return;
    }

    inDialog_ = false;
    currentTreeId_.clear();
    currentNodeId_.clear();

    if (onDialogEnd_) {
        onDialogEnd_();
    }

    CatEngine::Logger::info("Ended dialog");
}

void DialogSystem::setCurrentNode(const std::string& nodeId) {
    if (!inDialog_) {
        CatEngine::Logger::warning("Not in dialog");
        return;
    }

    const DialogTree* tree = getDialogTree(currentTreeId_);
    if (!tree) {
        CatEngine::Logger::error("Current dialog tree not found");
        return;
    }

    if (tree->nodes.find(nodeId) == tree->nodes.end()) {
        CatEngine::Logger::error("Dialog node not found: " + nodeId);
        return;
    }

    currentNodeId_ = nodeId;

    if (onNodeChange_) {
        onNodeChange_(nodeId);
    }
}

const DialogNode* DialogSystem::getCurrentNode() const {
    if (!inDialog_) {
        return nullptr;
    }

    return getNode(currentTreeId_, currentNodeId_);
}

const DialogNode* DialogSystem::getNode(const std::string& treeId, const std::string& nodeId) const {
    const DialogTree* tree = getDialogTree(treeId);
    if (!tree) {
        return nullptr;
    }

    auto it = tree->nodes.find(nodeId);
    return (it != tree->nodes.end()) ? &it->second : nullptr;
}

void DialogSystem::setVariable(const std::string& key, const std::string& value) {
    variables_[key] = value;
}

std::string DialogSystem::getVariable(const std::string& key) const {
    auto it = variables_.find(key);
    return (it != variables_.end()) ? it->second : "";
}

void DialogSystem::clearVariables() {
    variables_.clear();
}

std::string DialogSystem::substituteVariables(const std::string& text) const {
    std::string result = text;

    // Replace variables in format: $variableName or ${variableName}
    std::regex variableRegex(R"(\$\{?(\w+)\}?)");
    std::smatch match;

    std::string::const_iterator searchStart(result.cbegin());
    std::string output;

    while (std::regex_search(searchStart, result.cend(), match, variableRegex)) {
        // Append text before match
        output.append(searchStart, searchStart + match.position());

        // Get variable name (capturing group 1)
        std::string varName = match[1].str();
        std::string varValue = getVariable(varName);

        // If variable exists, use its value; otherwise, keep the original
        if (!varValue.empty()) {
            output.append(varValue);
        } else {
            output.append(match[0].str());
        }

        // Move search position
        searchStart = searchStart + match.position() + match.length();
    }

    // Append remaining text
    output.append(searchStart, result.cend());

    return output;
}

void DialogSystem::recordHistory(const std::string& npcName, const std::string& text) {
    DialogHistoryEntry entry;
    entry.dialogTreeId = currentTreeId_;
    entry.nodeId = currentNodeId_;
    entry.npcName = npcName;
    entry.text = text;
    entry.timestamp = static_cast<int>(history_.size());

    history_.push_back(entry);

    // Keep history size limited
    if (static_cast<int>(history_.size()) > historyMaxSize_) {
        history_.erase(history_.begin());
    }
}

void DialogSystem::clearHistory() {
    history_.clear();
}

void DialogSystem::printDialogTree(const std::string& treeId) const {
    const DialogTree* tree = getDialogTree(treeId);
    if (!tree) {
        CatEngine::Logger::error("Dialog tree not found: " + treeId);
        return;
    }

    CatEngine::Logger::info("Dialog Tree: " + tree->name + " (" + tree->id + ")");
    CatEngine::Logger::info("Start Node: " + tree->startNodeId);
    CatEngine::Logger::info("Nodes: " + std::to_string(tree->nodes.size()));

    for (const auto& pair : tree->nodes) {
        const DialogNode& node = pair.second;
        CatEngine::Logger::info("  Node: " + node.id);
        CatEngine::Logger::info("    Speaker: " + node.speakerName);
        CatEngine::Logger::info("    Text: " + node.text);
        CatEngine::Logger::info("    Options: " + std::to_string(node.options.size()));
    }
}

void DialogSystem::printCurrentNode() const {
    const DialogNode* node = getCurrentNode();
    if (!node) {
        CatEngine::Logger::warning("No current dialog node");
        return;
    }

    CatEngine::Logger::info("Current Node: " + node->id);
    CatEngine::Logger::info("Speaker: " + node->speakerName);
    CatEngine::Logger::info("Text: " + substituteVariables(node->text));
    CatEngine::Logger::info("Options:");

    for (size_t i = 0; i < node->options.size(); ++i) {
        const auto& option = node->options[i];
        CatEngine::Logger::info("  " + std::to_string(i) + ": " + substituteVariables(option.text));
    }
}

bool DialogSystem::parseDialogTree(const std::string& filepath, DialogTree& outTree) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        CatEngine::Logger::error("Failed to open dialog file: " + filepath);
        return false;
    }

    try {
        json data = json::parse(file);

        outTree.id = data.value("id", "");
        outTree.name = data.value("name", "");
        outTree.startNodeId = data.value("startNode", "");

        if (!data.contains("nodes") || !data["nodes"].is_array()) {
            CatEngine::Logger::error("Invalid dialog file: missing 'nodes' array");
            return false;
        }

        // Parse all nodes
        for (const auto& nodeJson : data["nodes"]) {
            DialogNode node;

            node.id = nodeJson.value("id", "");
            node.speakerName = nodeJson.value("speaker", "");
            node.text = nodeJson.value("text", "");
            node.autoAdvance = nodeJson.value("autoAdvance", false);
            node.autoAdvanceDelay = nodeJson.value("autoAdvanceDelay", 2.0f);
            node.nextNodeId = nodeJson.value("nextNode", "");

            // Parse options
            if (nodeJson.contains("options") && nodeJson["options"].is_array()) {
                for (const auto& optionJson : nodeJson["options"]) {
                    NPCDialogOption option;

                    option.text = optionJson.value("text", "");
                    option.responseId = optionJson.value("responseId", "");

                    if (optionJson.contains("requiredQuest")) {
                        option.requiredQuest = optionJson["requiredQuest"].get<std::string>();
                    }

                    if (optionJson.contains("requiredLevel")) {
                        option.requiredLevel = optionJson["requiredLevel"].get<int>();
                    }

                    if (optionJson.contains("requiredClan")) {
                        option.requiredClan = stringToClan(optionJson["requiredClan"].get<std::string>());
                    }

                    option.isAvailable = optionJson.value("isAvailable", true);

                    node.options.push_back(option);
                }
            }

            outTree.nodes[node.id] = node;
        }

        return true;

    } catch (const json::exception& e) {
        CatEngine::Logger::error("Failed to parse dialog file: " + std::string(e.what()));
        return false;
    }
}

bool DialogSystem::validateDialogTree(const DialogTree& tree) const {
    // Check basic fields
    if (tree.id.empty()) {
        CatEngine::Logger::error("Dialog tree has no ID");
        return false;
    }

    if (tree.nodes.empty()) {
        CatEngine::Logger::error("Dialog tree has no nodes: " + tree.id);
        return false;
    }

    if (tree.startNodeId.empty()) {
        CatEngine::Logger::error("Dialog tree has no start node: " + tree.id);
        return false;
    }

    // Check start node exists
    if (tree.nodes.find(tree.startNodeId) == tree.nodes.end()) {
        CatEngine::Logger::error("Start node not found in dialog tree: " + tree.id);
        return false;
    }

    // Validate all node references
    for (const auto& pair : tree.nodes) {
        const DialogNode& node = pair.second;

        // Check auto-advance next node
        if (node.autoAdvance && !node.nextNodeId.empty()) {
            if (tree.nodes.find(node.nextNodeId) == tree.nodes.end()) {
                CatEngine::Logger::warning("Invalid next node reference: " + node.nextNodeId + " in node: " + node.id);
            }
        }

        // Check option response nodes
        for (const auto& option : node.options) {
            if (!option.responseId.empty()) {
                if (tree.nodes.find(option.responseId) == tree.nodes.end()) {
                    CatEngine::Logger::warning("Invalid response node reference: " + option.responseId + " in node: " + node.id);
                }
            }
        }
    }

    return true;
}

} // namespace CatGame
