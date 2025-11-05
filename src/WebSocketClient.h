#pragma once
#include <functional>
#include <string>

class WebSocketClient {
  public:
    explicit WebSocketClient(const std::string& url);
    void send(const std::string& msg);

    std::function<void()> on_open;
    std::function<void(const std::string&)> on_message;
    std::function<void()> on_close;
    std::function<void(const std::string&)> on_error;

    int socket  {-1};
  
    std::function<void(const std::string&)> msg_cb;
    std::function<void()> open_cb;
    std::function<void()> close_cb;
    std::function<void(const std::string&)> err_cb;
};