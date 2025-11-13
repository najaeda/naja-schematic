#include "EquipotentialView.h"

#include <imgui.h>

#include "Types.h"

namespace {

ImColor getTopTermColor(Direction direction) {
    switch (direction) {
        case Direction::Input:
            return ImColor(255, 0, 0);   // Red
        case Direction::Output:
            return ImColor(0, 255, 0);   // Green
        case Direction::Inout:
            return ImColor(255, 255, 0); // Yellow
        default:
            return ImColor(255, 255, 255); // White
    }
}

ImColor getInstTermOccurrenceColor(Direction direction) {
    switch (direction) {
        case Direction::Input:
            return ImColor(0, 255, 0);   // Green
        case Direction::Output:
            return ImColor(255, 0, 0);   // Red
        case Direction::Inout:
            return ImColor(255, 255, 0); // Yellow
        default:
            return ImColor(255, 255, 255); // White
    }
}

} // anonymous namespace

void EquipotentialView::render(Equipotential* equipotential) {
    if (ImGui::BeginTable("equipotential_table", 3, 
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)
    ) {
        // ---- Header ----
        ImGui::TableSetupColumn("Path");
        ImGui::TableSetupColumn("Term");
        ImGui::TableSetupColumn("Direction");
        ImGui::TableHeadersRow();

        if (equipotential) {
            for (auto term: equipotential->terms) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", term.getString().c_str());
                ImGui::TableSetColumnIndex(2);
                ImU32 color = getTopTermColor(term.direction);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text("%s", toString(term.direction));
                ImGui::PopStyleColor();
            }

            for (const auto& occurrence : equipotential->occurrences) {
                const auto& term = occurrence.term;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string pathStr;
                for (size_t i = 0; i < occurrence.path.size(); ++i) {
                    pathStr += occurrence.path[i];;
                    if (i < occurrence.path.size() - 1) {
                        pathStr += ".";
                    }
                }
                ImGui::Text("%s", pathStr.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", term.getString().c_str());
                ImGui::TableSetColumnIndex(2);
                ImU32 color = getInstTermOccurrenceColor(term.direction);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text("%s", toString(term.direction));
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndTable();
    }
}