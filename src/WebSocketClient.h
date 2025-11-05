#pragma once
#include <functional>
#include <string>

class WebSocketClient {
  public:
    explicit WebSocketClient(const std::string& url);
    void send(const std::string& msg) const;

    void on_open(std::function<void()> callback);
    void on_message(std::function<void(const std::string&)> callback);
    void on_close(std::function<void()> callback);
    void on_error(std::function<void(const std::string&)> callback);

    int socket  {-1};
  
    std::function<void(const std::string&)> msg_cb;
    std::function<void()> open_cb;
    std::function<void()> close_cb;
    std::function<void(const std::string&)> err_cb;
};