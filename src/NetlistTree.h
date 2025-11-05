#pragma once

#include <string>
#include <vector>

#include "Types.h"

class NetlistTree;

class NetlistTreeNode {
  friend class NetlistTree;
  public:
    using Children = std::vector<NetlistTreeNode*>;

    NetlistTreeNode* getParent() const;
    NetlistTree* getTree() const;
    DesignRef getDesignRef() const;
    virtual std::string getLabel() const = 0;
    virtual bool isRoot() const { return false; }
    virtual void sendLoadRequest() const = 0;
  protected:
    NetlistTreeNode(NetlistTree* tree): parent_(tree) {}
  private:
    void render();

    void*       parent_               {nullptr};
    Children*   children_             {nullptr};
    bool        hasRequestedChildren_ {false};
};

class NetlistTreeInstanceNode : public NetlistTreeNode {
  public:
    NetlistTreeInstanceNode(NetlistTree* tree,
                            const std::string& name,
                            const DesignRef& design_ref);

    virtual bool isRoot() const override { return isRoot_; }
    virtual std::string getLabel() const override;
    virtual void sendLoadRequest() const override;
  private:
    bool        isRoot_     {false};
    std::string name_       {};
    DesignRef   design_ref_ {};
};

class WebSocketClient;

class NetlistTree {
  public:
    NetlistTree(const WebSocketClient* ws): ws_(ws) {}
    NetlistTree(const NetlistTree&) = delete;
    NetlistTree& operator=(const NetlistTree&) = delete;

    void createRoot(const std::string& name, const DesignRef& design_ref);
    NetlistTreeNode* getRoot() const { return root_; }
    const WebSocketClient* getWebSocketClient() const { return ws_; }

    void render();
  private:
    const WebSocketClient*  ws_   {nullptr};
    NetlistTreeNode*        root_ {nullptr};
};