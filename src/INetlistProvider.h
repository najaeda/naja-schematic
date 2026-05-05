#pragma once
#include <functional>
#include <string>

// Abstract interface for netlist data providers.
// - WebSocketProvider: connects to a remote Python/C++ server (WASM browser/VSCode)
// - LocalSNLProvider: loads netlists directly from naja SNL C++ API (standalone desktop)
class INetlistProvider {
  public:
    virtual ~INetlistProvider() = default;

    // Send a JSON-encoded request to the provider.
    virtual void send(const std::string& msg) = 0;

    // Register callbacks (mirrors the WebSocketClient API for easy wrapping).
    virtual void on_open(std::function<void()> callback) = 0;
    virtual void on_message(std::function<void(const std::string&)> callback) = 0;
    virtual void on_close(std::function<void()> callback) = 0;
    virtual void on_error(std::function<void(const std::string&)> callback) = 0;

    // Called after all callbacks are registered to start the provider.
    // WebSocketProvider: no-op (WebSocket connects in constructor).
    // LocalSNLProvider: fires on_open synchronously and delivers initial data.
    virtual void start() {}

    // Load a netlist from a file path.  No-op for providers that don't support it.
    virtual void loadFile(const std::string&) {}
};
