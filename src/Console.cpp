#include "Console.h"

#include <emscripten/val.h>

void Console::Log(const std::string& msg) {
    emscripten::val::global("console").call<void>("log", msg);
}

void Console::Error(const std::string& msg) {
    emscripten::val::global("console").call<void>("error", msg);
}