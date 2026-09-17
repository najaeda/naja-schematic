#include "NetlistTree.h"

#include <gtest/gtest.h>

#include "FakeNetlistProvider.h"

namespace {

DesignRef someDesign() { return DesignRef{1, 1, 1}; }

} // namespace

TEST(NetlistTree, RootPathKeyIsEmpty) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);
  tree.createRootNode("top", someDesign(), /*hasTerms=*/false,
                       /*hasPrimitives=*/false, /*hasInstances=*/true);

  EXPECT_TRUE(tree.getRoot()->isRoot());
  EXPECT_EQ(tree.getRoot()->getPathKey(), "");
}

// The tree only exposes children through render()/expand(), which draws via
// ImGui and isn't something a unit test should invoke. Instead this test
// walks the same create*Node calls AppLogic's message handler uses, then
// reaches the created node directly via NetlistTree::getNode(), which is
// populated by every node's constructor regardless of expand()/render().
TEST(NetlistTree, InstancePathKeyViaGetNode) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);
  tree.createRootNode("top", someDesign(), false, false, true);
  auto* root = tree.getRoot();

  root->createChildren();
  root->createInstanceNode("u1", "MOD_A", 1, someDesign(), false, false, true);
  // Root's guiID is 0 (first node inserted); u1 is inserted right after.
  auto* u1 = tree.getNode(1);
  ASSERT_NE(u1, nullptr);
  EXPECT_EQ(u1->getPathKey(), "u1");

  u1->createChildren();
  u1->createInstanceNode("u2", "MOD_B", 2, someDesign(), false, false, false);
  auto* u2 = tree.getNode(2);
  ASSERT_NE(u2, nullptr);
  EXPECT_EQ(u2->getPathKey(), "u1/u2");
}

TEST(NetlistTree, TermNodeBusBitsCountDownFromMsb) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);
  tree.createRootNode("top", someDesign(), true, false, false);
  auto* root = tree.getRoot();

  root->createChildren();
  root->createTermNode("data", 10, Direction::Input, /*msb=*/7, /*lsb=*/0);
  auto* term = tree.getNode(1);
  ASSERT_NE(term, nullptr);

  EXPECT_TRUE(term->isBus());
  EXPECT_FALSE(term->isBitTerm());
  auto bits = term->busBits();
  ASSERT_EQ(bits.size(), 8u);
  EXPECT_EQ(bits.front(), 7);
  EXPECT_EQ(bits.back(), 0);
}

TEST(NetlistTree, TermNodeBusBitsCountUpWhenLsbAboveMsb) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);
  tree.createRootNode("top", someDesign(), true, false, false);
  auto* root = tree.getRoot();

  root->createChildren();
  root->createTermNode("data", 10, Direction::Output, /*msb=*/0, /*lsb=*/3);
  auto* term = tree.getNode(1);
  ASSERT_NE(term, nullptr);

  auto bits = term->busBits();
  EXPECT_EQ(bits, (std::vector<int>{0, 1, 2, 3}));
}

TEST(NetlistTree, ScalarTermIsBitTermNotBus) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);
  tree.createRootNode("top", someDesign(), true, false, false);
  auto* root = tree.getRoot();

  root->createChildren();
  root->createTermNode("q", 11, Direction::Output, std::nullopt, std::nullopt);
  auto* term = tree.getNode(1);
  ASSERT_NE(term, nullptr);

  EXPECT_TRUE(term->isBitTerm());
  EXPECT_FALSE(term->isBus());
  EXPECT_TRUE(term->busBits().empty());
}

TEST(NetlistTree, SendLoadEquipotentialFormatsRequestAndFiresCallback) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);
  bool callbackFired = false;
  tree.setOnEquipotentialRequest([&] { callbackFired = true; });

  tree.sendLoadEquipotential({1, 2}, NetlistTree::TermID{9, true, 3});

  ASSERT_EQ(provider.sent.size(), 1u);
  EXPECT_EQ(provider.sent[0],
            R"({"request":"load_equipotential","path":[1,2],"term_id":9,"bit":3})");
  EXPECT_TRUE(callbackFired);
}

TEST(NetlistTree, SendLoadEquipotentialOmitsBitForNonBusBit) {
  FakeNetlistProvider provider;
  NetlistTree tree(&provider);

  tree.sendLoadEquipotential({}, NetlistTree::TermID{4, false, 0});

  ASSERT_EQ(provider.sent.size(), 1u);
  EXPECT_EQ(provider.sent[0], R"({"request":"load_equipotential","path":[],"term_id":4})");
}
