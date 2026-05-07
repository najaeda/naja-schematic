#include "NetlistTree.h"

#include <imgui.h>

#include "Console.h"
#include "INetlistProvider.h"

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

std::string NetlistTreeInstanceNode::getLabel() const {
  if (isRoot()) return name_.empty() ? "<unnamed root>" : name_;
  std::string label = name_.empty() ? "<unnamed>" : name_;
  if (!modelName_.empty()) label += " (" + modelName_ + ")";
  return label;
}

void NetlistTreeNode::getPath(NetlistTree::Path& path) const {
  getParent()->getPath(path);
}

void NetlistTreeNode::render() {
  expand();
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
  if (isLeaf()) {
    flags = flags | ImGuiTreeNodeFlags_Leaf;
  }
  if (getColor() != 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, getColor());
  }
  auto isOpen = ImGui::TreeNodeEx((void*)(intptr_t)guiID_, flags, "%s", getLabel().c_str());
  if (getColor() != 0) {
    ImGui::PopStyleColor();
  }
  if (isBitTerm()) {
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Show Equipotential")) {
        NetlistTree::Path path;
        getPath(path);
        getTree()->sendLoadEquipotential(
          path,
          NetlistTree::TermID{getChildID(), isBusBit(), getBusBit()});
      }
      ImGui::EndPopup();
    }
  }
  if (isOpen and not isLeaf()) {
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
  }
  if (isOpen) {
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
    isRoot_(true), name_(name),
    childID_(0),designRef_(designRef),
    hasTerms_(hasTerms), hasPrimitives_(hasPrimitives), hasInstances_(hasInstances)
{}

NetlistTreeInstanceNode::NetlistTreeInstanceNode(
    NetlistTreeNode* parent,
    const std::string& name,
    const std::string& modelName,
    unsigned childID,
    const DesignRef& designRef,
    bool hasTerms,
    bool hasPrimitives,
    bool hasInstances):
    NetlistTreeNode(parent),
    isRoot_(false), name_(name), modelName_(modelName),
    childID_(childID), designRef_(designRef),
    hasTerms_(hasTerms), hasPrimitives_(hasPrimitives), hasInstances_(hasInstances)
{}

void NetlistTreeInstanceNode::getPath(NetlistTree::Path& path) const {
  if (isRoot()) {
    path = NetlistTree::Path();
    return;
  } else {
    getParent()->getPath(path);
  }
  path.push_back(childID_);
}

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

NetlistTreeInstanceNode* NetlistTreeNode::getInstanceNode() const {
  return getParent()->getInstanceNode();
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
  getTree()->getProvider()->send(request);
}

NetlistTreeTermNode::NetlistTreeTermNode(
  NetlistTreeNode* parent,
  const std::string& name,
  unsigned childID,
  Direction direction,
  std::optional<int> msb,
  std::optional<int> lsb):
  NetlistTreeNode(parent), name_(name), childID_(childID), direction_(direction), msb_(msb), lsb_(lsb) {
  if (not msb_.has_value() or not lsb_.has_value()) { //scalar
    children_ = new Children();
  }
}

bool NetlistTreeTermNode::isTopTerm() const {
  return getInstanceNode()->isRoot();
}

size_t NetlistTreeTermNode::getWidth() const {
  return std::abs(msb_.value_or(0) - lsb_.value_or(0)) + 1;
}

void NetlistTreeTermNode::expand() {
  if (children_ == nullptr) {
    children_ = new Children();
    if (msb_.has_value() and lsb_.has_value()) {
      int msb = msb_.value();
      int lsb = lsb_.value();
      auto width = getWidth();
      for (size_t i=0; i<width; i++) {
        int bit = (msb>lsb)?msb-int(i):msb+int(i);
        children_->push_back(new NetlistTreeBusTermBitNode(
          this,
          bit
        ));
      }
    }
  }
}

std::string NetlistTreeTermNode::getLabel() const {
  std::string name;
  if (not name_.empty()) {
    name = name_;
  } else {
    name = "<unnamed>";
  }
  if (not isBitTerm()) {
    name += "[" + std::to_string(msb_.value()) + ":" + std::to_string(lsb_.value()) + "]";
  }
  return name_;
}

ImU32 NetlistTreeTermNode::getColor() const {
  switch (direction_) {
    case Direction::Input:
      if (isTopTerm()) {
        return IM_COL32(255, 0, 0, 255); // Red
      } else {
        return IM_COL32(0, 255, 0, 255); // Green
      }
    case Direction::Output:
      if (isTopTerm()) {
        return IM_COL32(0, 255, 0, 255); // Green
      } else {
        return IM_COL32(255, 0, 0, 255); // Red
      }
    case Direction::Inout:
      return IM_COL32(255, 255, 0, 255); // Yellow
    default:
      return 0;
  }
}

NetlistTreeBusTermBitNode::NetlistTreeBusTermBitNode(
  NetlistTreeTermNode* parent,
  int bit
): NetlistTreeNode(parent), bit_(bit)
{}

std::string NetlistTreeBusTermBitNode::getLabel() const {
  return std::to_string(bit_);
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
  unsigned childID,
  Direction direction,
  std::optional<int> msb,
  std::optional<int> lsb) {
  //this should be a group node of type Terms
  children_->push_back(new NetlistTreeTermNode(this, name, childID, direction, msb, lsb));
}

void NetlistTreeNode::createInstanceNode(
  const std::string& name,
  const std::string& modelName,
  unsigned childID,
  const DesignRef& design_ref,
  bool hasTerms,
  bool hasPrimitives,
  bool hasInstances) {
  auto node = new NetlistTreeInstanceNode(
    this, name, modelName,
    childID,
    design_ref,
    hasTerms, hasPrimitives, hasInstances);
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

void NetlistTree::sendLoadEquipotential(const NetlistTree::Path& path, const TermID& termID) const {
  std::string request = R"({"request":"load_equipotential",)";
  request += R"("path":[)";
  for (size_t i = 0; i < path.size(); ++i) {
    request += std::to_string(path[i]);
    if (i < path.size() - 1) {
      request += ",";
    }
  }
  request += R"(],)";
  request += R"("term_id":)" + std::to_string(termID.id);
  if (termID.isBusBit) {
    request += R"(,"bit":)" + std::to_string(termID.busBit);
  }
  request += R"(})";
  Console::Log("Sending load equipotential request: " + request);
  if (onEquipotentialRequest_) onEquipotentialRequest_();
  ws_->send(request);
}
