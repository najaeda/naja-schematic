#pragma once
#ifdef __EMSCRIPTEN__

#include "INetlistProvider.h"
#include "WebSocketClient.h"
#include <string>

// INetlistProvider implementation for browser/VSCode mode.
// Connects to a remote naja server via WebSocket.
class WebSocketProvider : public INetlistProvider {
  public:
    explicit WebSocketProvider(const std::string& url);

    void send(const std::string& msg) override;
    void on_open(std::function<void()> callback) override;
    void on_message(std::function<void(const std::string&)> callback) override;
    void on_close(std::function<void()> callback) override;
    void on_error(std::function<void(const std::string&)> callback) override;
    // start() is a no-op: WebSocket connects in constructor.

  private:
    WebSocketClient ws_;
};

#endif // __EMSCRIPTEN__
