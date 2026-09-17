#pragma once

#include <vector>

#include "INetlistProvider.h"

// Minimal INetlistProvider stand-in for tests that need a NetlistTree but
// never talk to a real backend. Records every send() so tests can assert
// on outgoing requests (e.g. load_equipotential) without a websocket or SNL.
class FakeNetlistProvider : public INetlistProvider {
  public:
    void send(const std::string& msg) override { sent.push_back(msg); }
    void on_open(std::function<void()>) override {}
    void on_message(std::function<void(const std::string&)>) override {}
    void on_close(std::function<void()>) override {}
    void on_error(std::function<void(const std::string&)>) override {}

    std::vector<std::string> sent;
};
