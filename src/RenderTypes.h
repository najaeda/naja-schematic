// RenderTypes.h
#pragma once
#include <string>
#include <vector>

// RenderTypes.h or the header that defines Port
struct Port {
  unsigned id;
  std::string name;
  // local normalized coords inside instance (-0.5 .. 0.5)
  float lx = 0.0f;   // add this
  float ly = 0.0f;   // add this
  bool isInput = true;
};


struct InstanceShape {
  unsigned id;
  std::string name;
  float x; // world coords center
  float y;
  float w; // width
  float h; // height
  uint32_t color;
  std::vector<Port> ports;
};

struct NetWire {
  unsigned id;
  unsigned srcInstance; // instance id
  unsigned srcPortId;
  unsigned dstInstance; // instance id
  unsigned dstPortId;
  uint32_t color;
};
