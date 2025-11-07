#pragma once

#include <string>
#include <vector>
#include "imgui.h"

#include "Types.h"

class NetlistTree;

class NetlistTreeNode {
  friend class NetlistTree;
  public:
    using Children = std::vector<NetlistTreeNode*>;

    NetlistTreeNode* getParent() const;
    NetlistTree* getTree() const;
    void createTermNode(
      const std::string& name,
      Direction direction,
      std::optional<int> msb,
      std::optional<int> lsb);
    void createInstanceNode(
      const std::string& name,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances);
    void createChildren();
    bool hasChildren() const { return children_ != nullptr; }
    virtual DesignRef getDesignRef() const;
    virtual void expand() {}
    virtual std::string getLabel() const = 0;
    virtual bool isRoot() const { return false; }
    virtual void sendLoadRequest() const = 0;
    virtual ImU32 getColor() const { return 0; }
    virtual bool isBitTerm() const {
      return false;
    }
  protected:
    NetlistTreeNode(NetlistTree* tree);
    NetlistTreeNode(NetlistTreeNode* parent);
    unsigned    guiID_                {0};
    Children*   children_             {nullptr};
  private:
    void render();

    void*       parent_               {nullptr};
    bool        hasRequestedChildren_ {false};
};

class NetlistTreeInstanceNode : public NetlistTreeNode {
  public:
    NetlistTreeInstanceNode(
      NetlistTree* tree,
      const std::string& name,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances
    );
    NetlistTreeInstanceNode(
      NetlistTreeNode* parent,
      const std::string& name,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances
    );

    virtual void expand() override;
    virtual bool isRoot() const override { return isRoot_; }
    virtual DesignRef getDesignRef() const override { return designRef_; }
    virtual std::string getLabel() const override;
    virtual void sendLoadRequest() const override;
  private:
    bool        isRoot_         {false};
    std::string name_           {};
    DesignRef   designRef_      {};
    bool        hasTerms_       {false};
    bool        hasPrimitives_  {false};
    bool        hasInstances_   {false};
};

class NetlistTreeGroupNode : public NetlistTreeNode {
  public:
    enum class Type {
      Terms,
      Primitives,
      Instances
    };
    NetlistTreeGroupNode(NetlistTreeNode* parent, Type type);

    virtual std::string getLabel() const override;
    virtual void sendLoadRequest() const override;
  private:
    Type type_;
};

class NetlistTreeTermNode : public NetlistTreeNode {
  public:
    NetlistTreeTermNode(NetlistTreeNode* parent,
                        const std::string& name,
                        Direction direction,
                        std::optional<int> msb,
                        std::optional<int> lsb);

    virtual std::string getLabel() const override;
    virtual ImU32 getColor() const override;
    virtual void expand() override;
    virtual void sendLoadRequest() const override {}
    virtual bool isBitTerm() const override {
      return !(msb_.has_value() && lsb_.has_value());
    }
  private:
    std::string         name_;
    Direction           direction_;
    std::optional<int>  msb_;
    std::optional<int>  lsb_;
};

class WebSocketClient;

class NetlistTree {
  public:
    using NodesMap = std::map<unsigned, NetlistTreeNode*>;
    NetlistTree(const WebSocketClient* ws): ws_(ws) {}
    NetlistTree(const NetlistTree&) = delete;
    NetlistTree& operator=(const NetlistTree&) = delete;

    void createRootNode(
      const std::string& name,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances);

    NetlistTreeNode* getNode(unsigned id) const;
    NetlistTreeNode* getRoot() const { return root_; }
    const WebSocketClient* getWebSocketClient() const { return ws_; }

    void render();
    void insertNodeInMap(NetlistTreeNode* node);
  private:
    const WebSocketClient*  ws_         {nullptr};
    NetlistTreeNode*        root_       {nullptr};
    unsigned                nextGUIID_ {0};
    NodesMap                nodes_;
};