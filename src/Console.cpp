#include "Console.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>

void Console::Log(const std::string& msg) {
  emscripten::val::global("console").call<void>("log", msg);
}

void Console::Error(const std::string& msg) {
  emscripten::val::global("console").call<void>("error", msg);
}

#else
#include <iostream>

void Console::Log(const std::string& msg) {
  std::cout << "[LOG] " << msg << "\n";
}

void Console::Error(const std::string& msg) {
  std::cerr << "[ERR] " << msg << "\n";
}

#endif
