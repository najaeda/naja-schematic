#pragma once

class Equipotential;

class EquipotentialView {
  public:
    static void renderSchematic(Equipotential* equipotential);
    static void renderTable(Equipotential* equipotential);
    static void zoomIn();
    static void zoomOut();
    static void fitView();
};
