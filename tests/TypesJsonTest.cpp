#include "Types.h"

#include <gtest/gtest.h>

TEST(DesignRefJson, ParsesAllFields) {
  json j = {{"db_id", 1}, {"library_id", 2}, {"design_id", 3}};
  DesignRef d = j.get<DesignRef>();
  EXPECT_EQ(d.db_id, 1);
  EXPECT_EQ(d.library_id, 2);
  EXPECT_EQ(d.design_id, 3);
}

TEST(DesignRefJson, ThrowsOnMissingField) {
  json j = {{"db_id", 1}, {"library_id", 2}};
  EXPECT_THROW(j.get<DesignRef>(), json::exception);
}

TEST(InstanceResponseJson, ParsesWithoutSourceLoc) {
  json j = {
    {"name", "u1"},
    {"model_name", "AND2"},
    {"child_id", 7},
    {"design_ref", {{"db_id", 1}, {"library_id", 2}, {"design_id", 3}}},
    {"has_primitives", true},
    {"has_instances", false},
    {"has_terms", true},
  };
  InstanceResponseJson r = j.get<InstanceResponseJson>();
  EXPECT_EQ(r.name, "u1");
  EXPECT_EQ(r.model_name, "AND2");
  EXPECT_EQ(r.child_id, 7u);
  EXPECT_TRUE(r.has_primitives);
  EXPECT_FALSE(r.has_instances);
  EXPECT_TRUE(r.has_terms);
  EXPECT_FALSE(r.source_loc.has_value());
}

TEST(InstanceResponseJson, ParsesSourceLocWithDefaults) {
  json j = {
    {"design_ref", {{"db_id", 1}, {"library_id", 2}, {"design_id", 3}}},
    {"has_primitives", false},
    {"has_instances", false},
    {"has_terms", false},
    {"source_loc", {{"file", "top.sv"}, {"line", 42}}},
  };
  InstanceResponseJson r = j.get<InstanceResponseJson>();
  ASSERT_TRUE(r.source_loc.has_value());
  EXPECT_EQ(r.source_loc->file, "top.sv");
  EXPECT_EQ(r.source_loc->line, 42);
  // end_line/column/end_column default from line/column when absent.
  EXPECT_EQ(r.source_loc->endLine, 42);
  EXPECT_EQ(r.source_loc->column, 0);
  EXPECT_EQ(r.source_loc->endColumn, 0);
}

TEST(InstanceResponseJson, MissingNameDefaultsEmpty) {
  json j = {
    {"design_ref", {{"db_id", 1}, {"library_id", 2}, {"design_id", 3}}},
    {"has_primitives", false},
    {"has_instances", false},
    {"has_terms", false},
  };
  InstanceResponseJson r = j.get<InstanceResponseJson>();
  EXPECT_EQ(r.name, "");
  EXPECT_EQ(r.child_id, 0u);
}

TEST(InstancesResponseJson, ParsesChildrenArray) {
  json j = {
    {"gui_id", 5},
    {"children", json::array({
      {
        {"name", "a"},
        {"design_ref", {{"db_id", 1}, {"library_id", 1}, {"design_id", 1}}},
        {"has_primitives", false},
        {"has_instances", false},
        {"has_terms", false},
      },
    })},
  };
  InstancesResponseJson r = j.get<InstancesResponseJson>();
  EXPECT_EQ(r.gui_id, 5);
  ASSERT_EQ(r.children.size(), 1u);
  EXPECT_EQ(r.children[0].name, "a");
}

TEST(InstancesResponseJson, MissingChildrenIsEmpty) {
  json j = {{"gui_id", 5}};
  InstancesResponseJson r = j.get<InstancesResponseJson>();
  EXPECT_TRUE(r.children.empty());
}

TEST(TermsResponseJson, ParsesScalarAndBusTerms) {
  json j = {
    {"gui_id", 1},
    {"children", json::array({
      {{"name", "q"}, {"child_id", 1}, {"direction", 1}},
      {{"name", "d"}, {"child_id", 2}, {"direction", 0}, {"msb", 7}, {"lsb", 0}},
    })},
  };
  TermsResponseJson r = j.get<TermsResponseJson>();
  ASSERT_EQ(r.children.size(), 2u);

  const auto& scalar = r.children[0];
  EXPECT_EQ(scalar.name, "q");
  EXPECT_EQ(scalar.direction, Direction::Output);
  EXPECT_FALSE(scalar.msb.has_value());
  EXPECT_FALSE(scalar.lsb.has_value());

  const auto& bus = r.children[1];
  EXPECT_EQ(bus.direction, Direction::Input);
  ASSERT_TRUE(bus.msb.has_value());
  ASSERT_TRUE(bus.lsb.has_value());
  EXPECT_EQ(*bus.msb, 7);
  EXPECT_EQ(*bus.lsb, 0);
}

TEST(EquipotentialJson, ParsesTermsAndOccurrences) {
  json j = {
    {"terms", json::array({
      {{"name", "q"}, {"direction", 1}, {"bit", 3}},
    })},
    {"occurrences", json::array({
      {
        {"path", json::array({json::array({"u1", 4}), json::array({"u2", 9})})},
        {"name", "d"},
        {"child_id", 2},
        {"direction", 0},
        {"has_instances", true},
        {"design_ref", {{"db_id", 1}, {"library_id", 1}, {"design_id", 1}}},
      },
    })},
  };
  Equipotential e = j.get<Equipotential>();

  ASSERT_EQ(e.terms.size(), 1u);
  EXPECT_EQ(e.terms[0].name, "q");
  ASSERT_TRUE(e.terms[0].bit.has_value());
  EXPECT_EQ(*e.terms[0].bit, 3);

  ASSERT_EQ(e.occurrences.size(), 1u);
  const auto& occ = e.occurrences[0];
  EXPECT_EQ(occ.path, (std::vector<std::string>{"u1", "u2"}));
  EXPECT_EQ(occ.pathIds, (std::vector<unsigned>{4u, 9u}));
  EXPECT_EQ(occ.term.name, "d");
  EXPECT_TRUE(occ.has_instances);
}

// Documents current behavior: occurrences parsing is nested inside the
// "terms is an array" check in Types.cpp, so a response with occurrences
// but no terms key parses to an empty Equipotential rather than throwing
// or populating occurrences.
TEST(EquipotentialJson, OccurrencesIgnoredWhenTermsKeyMissing) {
  json j = {
    {"occurrences", json::array({
      {{"name", "d"}, {"child_id", 2}},
    })},
  };
  Equipotential e = j.get<Equipotential>();
  EXPECT_TRUE(e.terms.empty());
  EXPECT_TRUE(e.occurrences.empty());
}

TEST(DiagnosisItemJson, ParsesInstanceKindWithDefaults) {
  json j = {
    {"path", json::array({"u1", "u2"})},
    {"severity", "error"},
    {"message", "stuck-at-0"},
    {"source", "kepler-formal"},
  };
  DiagnosisItem d = j.get<DiagnosisItem>();
  EXPECT_EQ(d.kind, DiagnosisKind::Instance);
  EXPECT_EQ(d.path, (std::vector<std::string>{"u1", "u2"}));
  EXPECT_EQ(d.terminal, "");
  EXPECT_EQ(d.severity, DiagnosisSeverity::Error);
  EXPECT_EQ(d.message, "stuck-at-0");
  EXPECT_EQ(d.source, "kepler-formal");
  EXPECT_EQ(d.pathKey(), "u1/u2");
}

TEST(DiagnosisItemJson, ParsesNetKindAndUnknownSeverityDefaultsInfo) {
  json j = {
    {"kind", "net"},
    {"terminal", "Q"},
    {"severity", "not-a-real-severity"},
  };
  DiagnosisItem d = j.get<DiagnosisItem>();
  EXPECT_EQ(d.kind, DiagnosisKind::Net);
  EXPECT_EQ(d.terminal, "Q");
  EXPECT_EQ(d.severity, DiagnosisSeverity::Info);
  EXPECT_TRUE(d.path.empty());
  EXPECT_EQ(d.pathKey(), "");
}

TEST(DiagnosisItemPathKey, EmptyPathIsTopLevel) {
  DiagnosisItem d;
  EXPECT_EQ(d.pathKey(), "");
}

TEST(GeometryHelpers, HierToggleGlyphRectIsCenteredOnTop) {
  InstanceShape inst;
  inst.x = 100.0f;
  inst.y = 50.0f;
  inst.w = 80.0f;
  inst.h = 40.0f;

  float x0, y0, x1, y1;
  hierToggleGlyphRect(inst, x0, y0, x1, y1);

  EXPECT_FLOAT_EQ(x1 - x0, 16.0f);
  EXPECT_FLOAT_EQ(y1 - y0, 16.0f);
  // Centered on inst's horizontal midpoint.
  EXPECT_FLOAT_EQ((x0 + x1) / 2.0f, inst.x + inst.w * 0.5f);
}

TEST(GeometryHelpers, CanShowHierToggleRequiresChildrenAndMinSize) {
  InstanceShape inst;
  inst.hasChildren = true;
  inst.w = 40.0f;
  inst.h = 30.0f;
  EXPECT_TRUE(canShowHierToggle(inst));

  inst.hasChildren = false;
  EXPECT_FALSE(canShowHierToggle(inst));

  inst.hasChildren = true;
  inst.w = 39.0f;
  EXPECT_FALSE(canShowHierToggle(inst));
}
