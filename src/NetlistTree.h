#pragma once

#include <string>

class NetlistTree;

class NetlistTreeNode {
  public:
    enum class Type {
        ROOT,
        INSTANCE
    };
    NetlistTreeNode(NetlistTree* tree);
  private:
    Type    type_;
    void*   parent_;
};

class NetlistTree {
  public:
    NetlistTree() = default;
    NetlistTree(const NetlistTree&) = delete;
    NetlistTree& operator=(const NetlistTree&) = delete;

    void createRoot(const std::string& name);
    NetlistTreeNode* getRoot() const { return root_; }
  private:
    NetlistTreeNode* root_  {nullptr};
};