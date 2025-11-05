#include "NetlistTree.h"

#include <imgui.h>

#include "WebSocketClient.h"

void NetlistTree::createRoot(const std::string& name, const DesignRef& design_ref) {
  root_ = new NetlistTreeInstanceNode(this, name, design_ref);
}

void NetlistTree::render() {
  if (root_) {
    root_->render();
  } else {
    ImGui::Text("Loading Root Node...");
  }
}

void NetlistTreeInstanceNode::sendLoadRequest() const {
    auto request = R"({"request":"load_instances","design_ref":{"db_id":)" +
        std::to_string(design_ref_.db_id) +
        R"(,"library_id":)" +
        std::to_string(design_ref_.library_id) +
        R"(,"design_id":)" +
        std::to_string(design_ref_.design_id) +
        R"(}})";
    getTree()->getWebSocketClient()->send(request);
}

std::string NetlistTreeInstanceNode::getLabel() const {
    std::string label;
    if (not name_.empty()) {
        label = name_;
    } else {
        if (isRoot()) {
            label = "<unnamed root>";
        } else {
            label = "<unnamed instance>";
        }
    }
    return label;
}

void NetlistTreeNode::render() {
  if (ImGui::TreeNodeEx(getLabel().c_str())) {
    if (children_ != nullptr) {
      // Render children here in the future
    } else {
      auto flags = ImGuiTreeNodeFlags_Leaf;
      ImGui::TreeNodeEx("Loading...", flags);
      if (not hasRequestedChildren_) {
        hasRequestedChildren_ = true;
        sendLoadRequest();
      }
      ImGui::TreePop();
    }
    // Render children here in the future
    ImGui::TreePop();
  }
}

NetlistTreeInstanceNode::NetlistTreeInstanceNode(
    NetlistTree* tree,
    const std::string& name,
    const DesignRef& design_ref):
    NetlistTreeNode(tree),
    isRoot_(true), name_(name), design_ref_(design_ref) {
}

NetlistTreeNode* NetlistTreeNode::getParent() const {
  if (isRoot()) {
    return nullptr;
  }
  return static_cast<NetlistTreeNode*>(parent_);
}

NetlistTree* NetlistTreeNode::getTree() const {
  if (isRoot()) {
    return static_cast<NetlistTree*>(parent_);
  }
  return getParent()->getTree();
}