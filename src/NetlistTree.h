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
    virtual DesignRef getDesignRef() const = 0;
    virtual std::string getLabel() const = 0;
    virtual bool isRoot() const { return false; }
    virtual void sendLoadRequest() const = 0;
  protected:
    NetlistTreeNode(NetlistTree* tree): parent_(tree) {}
    NetlistTreeNode(NetlistTreeNode* parent): parent_(parent) {}
    void createChildrenPlaceholders(bool hasTerms, bool hasPrimitives, bool hasInstances);
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
    virtual DesignRef getDesignRef() const override { return designRef_; }
    virtual std::string getLabel() const override;
    virtual void sendLoadRequest() const override;
  private:
    bool        isRoot_     {false};
    std::string name_       {};
    DesignRef   designRef_  {};
};

class NetlistTreeGroupNode : public NetlistTreeNode {
  public:
    enum class Type {
      Terms,
      Primitives,
      Instances
    };
    NetlistTreeGroupNode(NetlistTreeNode* parent, Type type);

    virtual DesignRef getDesignRef() const override;
    virtual std::string getLabel() const override;
    virtual void sendLoadRequest() const override;
  private:
    Type type_;
};

class WebSocketClient;

class NetlistTree {
  public:
    using NodesMap = std::map<unsigned, NetlistTreeNode*>;
    NetlistTree(const WebSocketClient* ws): ws_(ws) {}
    NetlistTree(const NetlistTree&) = delete;
    NetlistTree& operator=(const NetlistTree&) = delete;

    void createRoot(
      const std::string& name,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances);
    //void createTerms(
    //  unsigned parentGUID,
    //  const std::vector<InstanceResponseJson>& terms);

    NetlistTreeNode* getRoot() const { return root_; }
    const WebSocketClient* getWebSocketClient() const { return ws_; }

    void render();
    void insertNodeInMap(NetlistTreeNode* node);
  private:
    const WebSocketClient*  ws_         {nullptr};
    NetlistTreeNode*        root_       {nullptr};
    unsigned                nextNodeID_ {0};
    NodesMap                nodes_;
};