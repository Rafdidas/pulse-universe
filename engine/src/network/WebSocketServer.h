#pragma once

#include <memory>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "network/ServerConfig.h"

namespace pulse {

// 127.0.0.1 전용 WebSocket 브로드캐스트 서버.
// SystemSnapshot 을 모른다 — 완성된 문자열만 받아 연결된 세션들에 보낸다.
//
// setHello / broadcast / stop 은 어느 스레드에서 불러도 안전하다.
// 내부에서 io_context 로 post 하므로, 세션 목록은 io_context 스레드에서만
// 만져진다. 그래서 뮤텍스가 없다.
class WebSocketServer {
public:
    WebSocketServer(boost::asio::io_context& ioc, ServerConfig cfg);
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // 실제로 바인딩된 포트. cfg.port 가 0 이었다면 OS 가 고른 값이다.
    unsigned short port() const;

    // 새로 접속한 세션이 가장 먼저 받을 메시지.
    void setHello(std::shared_ptr<const std::string> hello);

    // 연결된 모든 세션에 보낸다. 보관되어 새 세션에도 hello 직후 전달된다.
    void broadcast(std::shared_ptr<const std::string> message);

    // 수락을 멈추고 세션을 모두 닫는다. io_context.run() 이 돌아오게 된다.
    void stop();

private:
    class Session;

    void doAccept();
    void onAccept(boost::system::error_code ec, boost::asio::ip::tcp::socket socket);
    void removeSession(const std::shared_ptr<Session>& session);

    boost::asio::io_context& ioc_;
    ServerConfig cfg_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<Session>> sessions_;
    std::shared_ptr<const std::string> hello_;
    std::shared_ptr<const std::string> latest_;
    bool stopped_ = false;
};

}  // namespace pulse
