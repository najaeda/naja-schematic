#include "NetlistTree.h"

#include <imgui.h>

#include "Console.h"
#include "WebSocketClient.h"

void NetlistTree::createRootNode(
  const std::string& name,
  const DesignRef& design_ref,
  bool hasTerms,
  bool hasPrimitives,
  bool hasInstances) {
  root_ = new NetlistTreeInstanceNode(this, name, design_ref, hasTerms, hasPrimitives, hasInstances);
}

void NetlistTree::render() {
  if (root_) {
    root_->render();
  } else {
    ImGui::Text("Loading Root Node...");
  }
}

void NetlistTree::insertNodeInMap(NetlistTreeNode* node) {
  node->guiID_ = nextGUIID_;
  nodes_[nextGUIID_++] = node;
}

NetlistTreeNode* NetlistTree::getNode(unsigned guiID) const {
  auto it = nodes_.find(guiID);
  return (it != nodes_.end()) ? it->second : nullptr;
}

void NetlistTreeInstanceNode::sendLoadRequest() const {}

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
  expand();
  auto flags = ImGuiTreeNodeFlags_None;
  if (children_ != nullptr and children_->empty()) {
    flags = ImGuiTreeNodeFlags_Leaf;
  }
  if (getColor() != 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, getColor());
  }
  auto isOpen = ImGui::TreeNodeEx(getLabel().c_str(), flags);
  if (getColor() != 0) {
    ImGui::PopStyleColor();
  }
  if (isBitTerm()) {
    if (ImGui::BeginPopupContextItem()) {
      ImGui::Text("Show Equipotential");
      ImGui::EndPopup();
    }
  }
  if (isOpen) {
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
    const DesignRef& designRef,
    bool hasTerms,
    bool hasPrimitives,
    bool hasInstances):
    NetlistTreeNode(tree),
    isRoot_(true), name_(name), designRef_(designRef),
    hasTerms_(hasTerms), hasPrimitives_(hasPrimitives), hasInstances_(hasInstances)
{}

NetlistTreeInstanceNode::NetlistTreeInstanceNode(
    NetlistTreeNode* parent,
    const std::string& name,
    const DesignRef& designRef,
    bool hasTerms,
    bool hasPrimitives,
    bool hasInstances):
    NetlistTreeNode(parent),
    isRoot_(false), name_(name), designRef_(designRef),
    hasTerms_(hasTerms), hasPrimitives_(hasPrimitives), hasInstances_(hasInstances)
{}

NetlistTreeGroupNode::NetlistTreeGroupNode(
  NetlistTreeNode* parent,
  Type type): NetlistTreeNode(parent), type_(type) {
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

DesignRef NetlistTreeNode::getDesignRef() const {
  return getParent()->getDesignRef();
}

void NetlistTreeGroupNode::sendLoadRequest() const {
  Console::Log("Sending load request for group: " + getLabel());
  std::string request;
  switch (type_) {
    case Type::Terms:
      request = R"({"request":"load_terms",)";
      break;
    case Type::Primitives:
      request = R"({"request":"load_primitives",)";
      break;
    case Type::Instances:
      request = R"({"request":"load_instances",)";
      break;
    default:
      Console::Error("Unknown group type for load request");
      return;
  }
  request += R"("gui_id":)" + std::to_string(guiID_) + ",";
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

NetlistTreeTermNode::NetlistTreeTermNode(
  NetlistTreeNode* parent,
  const std::string& name,
  Direction direction,
  std::optional<int> msb,
  std::optional<int> lsb):
  NetlistTreeNode(parent), name_(name), direction_(direction), msb_(msb), lsb_(lsb) {
  if (not msb_.has_value() or not lsb_.has_value()) { //scalar
    children_ = new Children();
  }
}

void NetlistTreeTermNode::expand() {
  if (children_ == nullptr) {
    children_ = new Children();
    if (msb_.has_value() and lsb_.has_value()) {

    }
  }
}

std::string NetlistTreeTermNode::getLabel() const {
  if (not name_.empty()) {
    return name_;
  } else {
    return "<unnamed>";
  }
  return name_;
}

ImU32 NetlistTreeTermNode::getColor() const {
  switch (direction_) {
    case Direction::Input:
      return IM_COL32(0, 255, 0, 255); // Green
    case Direction::Output:
      return IM_COL32(255, 0, 0, 255); // Red
    case Direction::Inout:
      return IM_COL32(255, 255, 0, 255); // Yellow
    default:
      return 0;
  }
}

void NetlistTreeNode::createChildren() {
  children_ = new Children();
}

void NetlistTreeInstanceNode::expand() {
  if (children_ != nullptr) {
    return;
  }
  children_ = new Children();
  if (hasTerms_) {
    children_->push_back(new NetlistTreeGroupNode(this, NetlistTreeGroupNode::Type::Terms));
  }
  if (hasPrimitives_) {
    children_->push_back(new NetlistTreeGroupNode(this, NetlistTreeGroupNode::Type::Primitives));
  }
  if (hasInstances_) {
    children_->push_back(new NetlistTreeGroupNode(this, NetlistTreeGroupNode::Type::Instances));
  }
}

void NetlistTreeNode::createTermNode(
  const std::string& name,
  Direction direction,
  std::optional<int> msb,
  std::optional<int> lsb) {
  //this should be a group node of type Terms
  children_->push_back(new NetlistTreeTermNode(this, name, direction, msb, lsb));
}

void NetlistTreeNode::createInstanceNode(
  const std::string& name,
  const DesignRef& design_ref,
  bool hasTerms,
  bool hasPrimitives,
  bool hasInstances) {
  auto node = new NetlistTreeInstanceNode(this, name, design_ref, hasTerms, hasPrimitives, hasInstances);
  children_->push_back(node);
}

NetlistTreeNode::NetlistTreeNode(NetlistTree* tree): parent_(tree) {
  tree->insertNodeInMap(this);
}

NetlistTreeNode::NetlistTreeNode(NetlistTreeNode* parent): parent_(parent) {
  getTree()->insertNodeInMap(this);
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