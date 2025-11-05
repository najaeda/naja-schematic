#include "WebSocketClient.h"

#include <iostream>
#include <emscripten/websocket.h>
#include <emscripten/val.h>

EM_BOOL on_open(int eventType, const EmscriptenWebSocketOpenEvent*, void* userData) {
    auto* client = reinterpret_cast<WebSocketClient*>(userData);
    std::cout << "✅ WebSocket connected!\n";
    if (client->open_cb) client->open_cb();
    return EM_TRUE;
}

EM_BOOL on_message(int eventType, const EmscriptenWebSocketMessageEvent* e, void* userData) {
    auto* client = reinterpret_cast<WebSocketClient*>(userData);
    if (e->isText && e->data) {
        std::string msg(reinterpret_cast<const char*>(e->data), e->numBytes);
        std::cout << "💬 Received: " << msg << "\n";
        if (client->msg_cb) client->msg_cb(msg);
    }
    return EM_TRUE;
}

EM_BOOL on_close(int eventType, const EmscriptenWebSocketCloseEvent*, void* userData) {
    auto* client = reinterpret_cast<WebSocketClient*>(userData);
    std::cout << "❎ WebSocket closed.\n";
    if (client->close_cb) client->close_cb();
    return EM_TRUE;
}

EM_BOOL on_error(int eventType, const EmscriptenWebSocketErrorEvent*, void* userData) {
    auto* client = reinterpret_cast<WebSocketClient*>(userData);
    std::cerr << "⚠️ WebSocket error.\n";
    if (client->err_cb) client->err_cb("WebSocket error");
    return EM_TRUE;
}

WebSocketClient::WebSocketClient(const std::string& url) {
    if (!emscripten_websocket_is_supported()) {
        std::cerr << "❌ WebSockets not supported in this browser.\n";
        socket = -1;
        return;
    }

    EmscriptenWebSocketCreateAttributes attr;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = url.c_str();
    attr.createOnMainThread = EM_TRUE;

    socket = emscripten_websocket_new(&attr);
    if (socket <= 0) {
        std::cerr << "❌ Failed to create WebSocket: " << url << "\n";
        return;
    }

    emscripten_websocket_set_onopen_callback(socket, this, ::on_open);
    emscripten_websocket_set_onmessage_callback(socket, this, ::on_message);
    emscripten_websocket_set_onclose_callback(socket, this, ::on_close);
    emscripten_websocket_set_onerror_callback(socket, this, ::on_error);
}

void WebSocketClient::send(const std::string& msg) {
    if (socket > 0) {
        emscripten_websocket_send_utf8_text(socket, msg.c_str());
        std::cout << "📨 Sent: " << msg << "\n";
    }
}