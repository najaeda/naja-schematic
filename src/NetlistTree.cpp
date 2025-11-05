#include "NetlistTree.h"

void NetlistTree::createRoot(const std::string& name) {
    root_ = new NetlistTreeNode(this);
}

NetlistTreeNode::NetlistTreeNode(NetlistTree* tree)
    : type_(Type::ROOT), parent_(tree) {
}