#pragma once

#include <string>
#include <vector>
#include "imgui.h"

#include "Types.h"

class NetlistTreeNode;
class INetlistProvider;


class NetlistTree {
  public:
    struct TermID {
      unsigned id;
      bool isBusBit;
      int busBit;
    };
    using Path = std::vector<unsigned>;
    using NodesMap = std::map<unsigned, NetlistTreeNode*>;
    NetlistTree(INetlistProvider* provider): ws_(provider) {}
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
    void sendLoadEquipotential(const Path& path, const TermID& termID) const;
    INetlistProvider* getProvider() const { return ws_; }

    // Called before every tree-initiated equipotential request.
    // Use this to reset the schematic and table views.
    void setOnEquipotentialRequest(std::function<void()> cb) {
      onEquipotentialRequest_ = std::move(cb);
    }

    void render();
    void insertNodeInMap(NetlistTreeNode* node);
  private:
    INetlistProvider*       ws_                    {nullptr};
    std::function<void()>   onEquipotentialRequest_;
    NetlistTreeNode*        root_       {nullptr};
    unsigned                nextGUIID_ {0};
    NodesMap                nodes_;
};

class NetlistTreeInstanceNode;

class NetlistTreeNode {
  friend class NetlistTree;
  public:
    using Children = std::vector<NetlistTreeNode*>;

    NetlistTreeNode* getParent() const;
    NetlistTree* getTree() const;
    void createTermNode(
      const std::string& name,
      unsigned childID,
      Direction direction,
      std::optional<int> msb,
      std::optional<int> lsb);
    void createInstanceNode(
      const std::string& name,
      const std::string& modelName,
      unsigned charID,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances);
    void createChildren();
    bool hasChildren() const { return children_ != nullptr; }
    virtual NetlistTreeInstanceNode* getInstanceNode() const;
    virtual DesignRef getDesignRef() const;
    virtual void expand() {}
    virtual std::string getLabel() const = 0;
    virtual bool isRoot() const { return false; }
    virtual void sendLoadRequest() const {}
    virtual ImU32 getColor() const { return 0; }
    virtual unsigned getChildID() const { return 0; }
    virtual bool isBusBit() const {
      return false;
    }
    virtual bool isLeaf() const {
      return false;
    }
    virtual bool isBitTerm() const {
      return false;
    }
    virtual int getBusBit() const {
      return 0;
    }
    virtual void getPath(NetlistTree::Path& path) const;
    // Slash-joined instance-name path, root excluded ("" at/above root).
    // Matches DiagnosisItem::pathKey() and EquipotentialView's instance keys.
    virtual std::string getPathKey() const;
    // Diagnosis items attached to this node's path, if any (only
    // NetlistTreeInstanceNode currently reports these).
    virtual std::vector<const DiagnosisItem*> getDiagnostics() const { return {}; }
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
      const std::string& modelName,
      unsigned childID,
      const DesignRef& design_ref,
      bool hasTerms,
      bool hasPrimitives,
      bool hasInstances
    );

    virtual void expand() override;
    virtual bool isRoot() const override { return isRoot_; }
    virtual DesignRef getDesignRef() const override { return designRef_; }
    virtual std::string getLabel() const override;
    virtual void getPath(NetlistTree::Path& path) const override;
    virtual std::string getPathKey() const override;
    virtual unsigned getChildID() const override { return childID_; }
    virtual bool isLeaf() const override {
      return !(hasTerms_ || hasPrimitives_ || hasInstances_);
    }
    virtual NetlistTreeInstanceNode* getInstanceNode() const override {
      return const_cast<NetlistTreeInstanceNode*>(this);
    }
    virtual ImU32 getColor() const override;
    virtual std::vector<const DiagnosisItem*> getDiagnostics() const override;
  private:
    bool        isRoot_         {false};
    std::string name_           {};
    std::string modelName_      {};
    unsigned    childID_        {0};
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
    virtual bool isLeaf() const override {
      return false;
    }
  private:
    Type type_;
};

class NetlistTreeTermNode : public NetlistTreeNode {
  public:
    NetlistTreeTermNode(NetlistTreeNode* parent,
                        const std::string& name,
                        unsigned childID,
                        Direction direction,
                        std::optional<int> msb,
                        std::optional<int> lsb);

    virtual std::string getLabel() const override;
    virtual ImU32 getColor() const override;
    virtual void expand() override;
    virtual bool isLeaf() const override {
      return isBitTerm();
    }
    virtual bool isBitTerm() const override {
      return !(msb_.has_value() && lsb_.has_value());
    }
    virtual unsigned getChildID() const override { return childID_; }
    bool isTopTerm() const;
    size_t getWidth() const;
  private:
    std::string         name_;
    unsigned            childID_;
    Direction           direction_;
    std::optional<int>  msb_;
    std::optional<int>  lsb_;
};

class NetlistTreeBusTermBitNode : public NetlistTreeNode {
  public:
    NetlistTreeBusTermBitNode(
      NetlistTreeTermNode* parent,
      int bit
    );
    virtual bool isBitTerm() const override { return true; }
    virtual std::string getLabel() const override;
    virtual bool isLeaf() const override {
      return true;
    }
    virtual bool isBusBit() const override {
      return true;
    }
    virtual int getBusBit() const override{
      return bit_;
    }
    virtual unsigned getChildID() const override {
      return getParent()->getChildID();
    }
    bool isTopTerm() const {
      return getInstanceNode()->isRoot();
    }
  private:
    int bit_;
};