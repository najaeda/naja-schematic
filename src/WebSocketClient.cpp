#include "WebSocketClient.h"

#include <iostream>
#include <emscripten/websocket.h>
#include <emscripten/val.h>

#include "Console.h"

EM_BOOL on_open(int eventType, const EmscriptenWebSocketOpenEvent*, void* userData) {
  auto* client = reinterpret_cast<WebSocketClient*>(userData);
  Console::Log("✅ WebSocket connected!");
  if (client->open_cb) {
    Console::Log("✅ WebSocket Opening callback called!");
    client->open_cb();
  } else {
    Console::Log("✅ WebSocket Opening callback not set.");
  }
  return EM_TRUE;
}

EM_BOOL on_message(int eventType, const EmscriptenWebSocketMessageEvent* e, void* userData) {
  auto* client = reinterpret_cast<WebSocketClient*>(userData);
  if (e->isText && e->data) {
    std::string msg(reinterpret_cast<const char*>(e->data), e->numBytes);
    Console::Log("💬 Received: " + msg);
    if (client->msg_cb) client->msg_cb(msg);
  }
  return EM_TRUE;
}

EM_BOOL on_close(int eventType, const EmscriptenWebSocketCloseEvent*, void* userData) {
  auto* client = reinterpret_cast<WebSocketClient*>(userData);
  Console::Log("❎ WebSocket closed.");
  if (client->close_cb) client->close_cb();
  return EM_TRUE;
}

EM_BOOL on_error(int eventType, const EmscriptenWebSocketErrorEvent*, void* userData) {
  auto* client = reinterpret_cast<WebSocketClient*>(userData);
  Console::Error("⚠️ WebSocket error.");
  if (client->err_cb) client->err_cb("WebSocket error");
  return EM_TRUE;
}

WebSocketClient::WebSocketClient(const std::string& url) {
  if (!emscripten_websocket_is_supported()) {
    Console::Error("WebSockets not supported in this browser.");
    socket = -1;
    return;
  }

  EmscriptenWebSocketCreateAttributes attr;
  emscripten_websocket_init_create_attributes(&attr);
  attr.url = url.c_str();
  attr.createOnMainThread = EM_TRUE;

  socket = emscripten_websocket_new(&attr);
  if (socket <= 0) {
    Console::Error("Failed to create WebSocket: " + url);
    return;
  }

  emscripten_websocket_set_onopen_callback(socket, this, ::on_open);
  emscripten_websocket_set_onmessage_callback(socket, this, ::on_message);
  emscripten_websocket_set_onclose_callback(socket, this, ::on_close);
  emscripten_websocket_set_onerror_callback(socket, this, ::on_error);
}

void WebSocketClient::send(const std::string& msg) const {
  if (socket > 0) {
    emscripten_websocket_send_utf8_text(socket, msg.c_str());
    Console::Log( "Sent: " + msg );
  }
}

void WebSocketClient::on_open(std::function<void()> callback) {
  open_cb = std::move(callback);
}

void WebSocketClient::on_message(std::function<void(const std::string&)> callback) {
  msg_cb = std::move(callback);
}

void WebSocketClient::on_close(std::function<void()> callback) {
  close_cb = std::move(callback);
}

void WebSocketClient::on_error(std::function<void(const std::string&)> callback) {
  err_cb = std::move(callback);
}