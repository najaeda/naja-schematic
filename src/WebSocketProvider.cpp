#ifdef __EMSCRIPTEN__

#include "WebSocketProvider.h"

WebSocketProvider::WebSocketProvider(const std::string& url) : ws_(url) {}

void WebSocketProvider::send(const std::string& msg) {
  ws_.send(msg);
}

void WebSocketProvider::on_open(std::function<void()> callback) {
  ws_.on_open(std::move(callback));
}

void WebSocketProvider::on_message(std::function<void(const std::string&)> callback) {
  ws_.on_message(std::move(callback));
}

void WebSocketProvider::on_close(std::function<void()> callback) {
  ws_.on_close(std::move(callback));
}

void WebSocketProvider::on_error(std::function<void(const std::string&)> callback) {
  ws_.on_error(std::move(callback));
}

#endif // __EMSCRIPTEN__
