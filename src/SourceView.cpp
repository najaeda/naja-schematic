#include "SourceView.h"

#include <imgui.h>

#include "SourceStore.h"

void SourceView::render() {
  if (!SourceStore::hasSource()) {
    ImGui::TextDisabled("No RTL source loaded.");
    ImGui::TextWrapped(
      "Right-click an instance in the tree or on a schematic box and choose "
      "\"Show RTL Source\" (only available for SystemVerilog-loaded "
      "instances with a recorded source location).");
    return;
  }

  bool scrollToTarget = SourceStore::takePendingScroll();
  const auto& lines   = SourceStore::lines();
  int target          = SourceStore::targetLine();

  ImGui::TextUnformatted(SourceStore::file().c_str());
  ImGui::Separator();

  ImGui::BeginChild("##SourceLines", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

  float lineH = ImGui::GetTextLineHeightWithSpacing();
  if (scrollToTarget && target > 0) {
    float childH = ImGui::GetWindowHeight();
    float y = float(target - 1) * lineH - childH * 0.35f;
    ImGui::SetScrollY(y > 0.0f ? y : 0.0f);
  }

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(lines.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      int lineNo   = i + 1;
      bool isTarget = (lineNo == target);
      if (isTarget) {
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        ImVec2 rowMax(rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + lineH);
        ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax, IM_COL32(90, 70, 20, 140));
      }
      ImGui::TextDisabled("%4d ", lineNo);
      ImGui::SameLine();
      ImGui::TextUnformatted(lines[i].c_str());
    }
  }
  clipper.End();

  ImGui::EndChild();
}
