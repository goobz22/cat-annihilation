#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "NPCSystem.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace CatGame {

/**
 * Dialog variable for substitution
 */
struct DialogVariable {
    std::string key;
    std::string value;
};

/**
 * Dialog history entry
 */
struct DialogHistoryEntry {
    std::string dialogTreeId;
    std::string nodeId;
    std::string npcName;
    std::string text;
    int timestamp;
};

/**
 * Dialog tree - collection of dialog nodes
 */
struct DialogTree {
    std::string id;
    std::string name;
    std::string startNodeId;
    std::unordered_map<std::string, DialogNode> nodes;
};

/**
 * Dialog System
 * Manages dialog trees, variable substitution, and conversation flow
 */
class DialogSystem : public CatEngine::System {
public:
    using DialogStartCallback = std::function<void(const std::string& treeId)>;
    using DialogEndCallback = std::function<void()>;
    using NodeChangeCallback = std::function<void(const std::string& nodeId)>;

    explicit DialogSystem(int priority = 140);
    ~DialogSystem() override = default;

    void init(CatEngine::ECS* ecs) override;
    void update(float dt) override;
    const char* getName() const override { return "DialogSystem"; }

    /**
     * Dialog tree management
     */
    bool loadDialogTree(const std::string& filepath);
    bool loadDialogTreesFromDirectory(const std::string& directory);
    const DialogTree* getDialogTree(const std::string& treeId) const;
    bool hasDialogTree(const std::string& treeId) const;

    /**
     * Dialog flow control
     */
    void startDialog(const std::string& treeId);
    void endDialog();
    bool isInDialog() const { return inDialog_; }
    const std::string& getCurrentTreeId() const { return currentTreeId_; }
    const std::string& getCurrentNodeId() const { return currentNodeId_; }

    /**
     * Node navigation
     */
    void setCurrentNode(const std::string& nodeId);
    const DialogNode* getCurrentNode() const;
    const DialogNode* getNode(const std::string& treeId, const std::string& nodeId) const;

    /**
     * Variable substitution
     */
    void setVariable(const std::string& key, const std::string& value);
    std::string getVariable(const std::string& key) const;
    void clearVariables();
    std::string substituteVariables(const std::string& text) const;

    /**
     * Dialog history
     */
    void recordHistory(const std::string& npcName, const std::string& text);
    const std::vector<DialogHistoryEntry>& getHistory() const { return history_; }
    void clearHistory();

    /**
     * Fast-forward / Skip
     */
    void setSkipEnabled(bool enabled) { skipEnabled_ = enabled; }
    bool isSkipEnabled() const { return skipEnabled_; }
    void setFastForwardSpeed(float speed) { fastForwardSpeed_ = speed; }
    float getFastForwardSpeed() const { return fastForwardSpeed_; }

    /**
     * Callbacks
     */
    void setOnDialogStart(DialogStartCallback callback) { onDialogStart_ = callback; }
    void setOnDialogEnd(DialogEndCallback callback) { onDialogEnd_ = callback; }
    void setOnNodeChange(NodeChangeCallback callback) { onNodeChange_ = callback; }

    /**
     * Debugging
     */
    void printDialogTree(const std::string& treeId) const;
    void printCurrentNode() const;

private:
    /**
     * Parse dialog tree from JSON
     */
    bool parseDialogTree(const std::string& filepath, DialogTree& outTree);

    /**
     * Validate dialog tree
     */
    bool validateDialogTree(const DialogTree& tree) const;

    // Dialog trees storage
    std::unordered_map<std::string, DialogTree> dialogTrees_;

    // Current dialog state
    bool inDialog_ = false;
    std::string currentTreeId_;
    std::string currentNodeId_;

    // Variables for substitution
    std::unordered_map<std::string, std::string> variables_;

    // Dialog history
    std::vector<DialogHistoryEntry> history_;
    int historyMaxSize_ = 100;

    // Skip/fast-forward settings
    bool skipEnabled_ = true;
    float fastForwardSpeed_ = 2.0f;

    // Callbacks
    DialogStartCallback onDialogStart_;
    DialogEndCallback onDialogEnd_;
    NodeChangeCallback onNodeChange_;
};

} // namespace CatGame
