#pragma once

class Equipotential;

class EquipotentialView {
  public:
    static void render(Equipotential* equipotential);
    static void zoomIn();
    static void zoomOut();
    static void fitView();
};
