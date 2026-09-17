#include "SourceStore.h"

#include <gtest/gtest.h>

class SourceStoreTest : public ::testing::Test {
  protected:
    void TearDown() override { SourceStore::clear(); }
};

TEST_F(SourceStoreTest, StartsEmpty) {
  EXPECT_FALSE(SourceStore::hasSource());
  EXPECT_TRUE(SourceStore::lines().empty());
}

TEST_F(SourceStoreTest, SplitsLfSeparatedLines) {
  SourceStore::setSource("top.sv", 2, "line0\nline1\nline2");
  ASSERT_TRUE(SourceStore::hasSource());
  EXPECT_EQ(SourceStore::file(), "top.sv");
  EXPECT_EQ(SourceStore::targetLine(), 2);
  ASSERT_EQ(SourceStore::lines().size(), 3u);
  EXPECT_EQ(SourceStore::lines()[0], "line0");
  EXPECT_EQ(SourceStore::lines()[1], "line1");
  EXPECT_EQ(SourceStore::lines()[2], "line2");
}

TEST_F(SourceStoreTest, StripsTrailingCarriageReturn) {
  SourceStore::setSource("top.sv", 0, "line0\r\nline1\r\n");
  ASSERT_EQ(SourceStore::lines().size(), 3u);
  EXPECT_EQ(SourceStore::lines()[0], "line0");
  EXPECT_EQ(SourceStore::lines()[1], "line1");
  EXPECT_EQ(SourceStore::lines()[2], "");
}

TEST_F(SourceStoreTest, EmptyTextYieldsOneEmptyLine) {
  SourceStore::setSource("top.sv", 0, "");
  ASSERT_EQ(SourceStore::lines().size(), 1u);
  EXPECT_EQ(SourceStore::lines()[0], "");
}

TEST_F(SourceStoreTest, ClearResetsState) {
  SourceStore::setSource("top.sv", 5, "a\nb");
  SourceStore::clear();
  EXPECT_FALSE(SourceStore::hasSource());
  EXPECT_EQ(SourceStore::file(), "");
  EXPECT_TRUE(SourceStore::lines().empty());
  EXPECT_EQ(SourceStore::targetLine(), 0);
}

TEST_F(SourceStoreTest, PendingScrollIsConsumedExactlyOnce) {
  SourceStore::setSource("top.sv", 3, "a\nb");
  EXPECT_TRUE(SourceStore::takePendingScroll());
  EXPECT_FALSE(SourceStore::takePendingScroll());
}

TEST_F(SourceStoreTest, NewSourceRearmsPendingScroll) {
  SourceStore::setSource("a.sv", 1, "x");
  EXPECT_TRUE(SourceStore::takePendingScroll());
  SourceStore::setSource("b.sv", 2, "y");
  EXPECT_TRUE(SourceStore::takePendingScroll());
}
