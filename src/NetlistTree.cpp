#include "NetlistTree.h"

#include <imgui.h>

#include "Console.h"
#include "WebSocketClient.h"

void NetlistTree::createRoot(
  const std::string& name,
  const DesignRef& design_ref,
  bool hasTerms,
  bool hasPrimitives,
  bool hasInstances) {
  root_ = new NetlistTreeInstanceNode(this, name, design_ref);
  root_->createChildrenPlaceholders(hasTerms, hasPrimitives, hasInstances);
}

void NetlistTree::render() {
  if (root_) {
    root_->render();
  } else {
    ImGui::Text("Loading Root Node...");
  }
}

void NetlistTree::insertNodeInMap(NetlistTreeNode* node) {
    nodes_[nextNodeID_++] = node;
}

void NetlistTreeInstanceNode::sendLoadRequest() const {
    auto request = R"({"request":"load_instances","design_ref":{"db_id":)" +
        std::to_string(designRef_.db_id) +
        R"(,"library_id":)" +
        std::to_string(designRef_.library_id) +
        R"(,"design_id":)" +
        std::to_string(designRef_.design_id) +
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
      for (auto child : *children_) {
        child->render();
      }
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
    const DesignRef& designRef):
    NetlistTreeNode(tree),
    isRoot_(true), name_(name), designRef_(designRef) {
  tree->insertNodeInMap(this);
}

NetlistTreeGroupNode::NetlistTreeGroupNode(
  NetlistTreeNode* parent,
  Type type): NetlistTreeNode(parent), type_(type) {
  getTree()->insertNodeInMap(this);
}

std::string NetlistTreeGroupNode::getLabel() const {
  switch (type_) {
    case Type::Terms:
      return "Terms";
    case Type::Primitives:
      return "Primitives";
    case Type::Instances:
      return "Instances";
    default:
      return "Unknown Group";
  }
}

DesignRef NetlistTreeGroupNode::getDesignRef() const {
  return getParent()->getDesignRef();
}

void NetlistTreeGroupNode::sendLoadRequest() const {
  Console::Log("Sending load request for group: " + getLabel());
  std::string request;
  switch (type_) {
    case Type::Terms:
      request = R"({"request":"load_terms", )";
      break;
    case Type::Primitives:
      request = R"({"request":"load_primitives", )";
      break;
    case Type::Instances:
      request = R"({"request":"load_instances", )";
      break;
    default:
      Console::Error("Unknown group type for load request");
      return;
  }
  auto designRef = getDesignRef();
  request += R"("design_ref":{"db_id":)" +
        std::to_string(designRef.db_id) +
        R"(,"library_id":)" +
        std::to_string(designRef.library_id) +
        R"(,"design_id":)" +
        std::to_string(designRef.design_id) +
        R"(}})";
  Console::Log("Sending load request: " + request);
  getTree()->getWebSocketClient()->send(request);
}

void NetlistTreeNode::createChildrenPlaceholders(
    bool hasTerms,
    bool hasPrimitives,
    bool hasInstances) {
  if (hasTerms or hasPrimitives or hasInstances) {
    children_ = new Children();
  }
  if (hasTerms) {
    Console::Log("Creating Terms placeholder");
    children_->push_back(new NetlistTreeGroupNode(this, NetlistTreeGroupNode::Type::Terms));
  }
  if (hasPrimitives) {
    children_->push_back(new NetlistTreeGroupNode(this, NetlistTreeGroupNode::Type::Primitives));
  }
  if (hasInstances) {
    children_->push_back(new NetlistTreeGroupNode(this, NetlistTreeGroupNode::Type::Instances));
  }
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