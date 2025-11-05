#pragma once

#include <string>

class Console {
  public:
    static void Log(const std::string& msg);
    static void Error(const std::string& msg);
};
