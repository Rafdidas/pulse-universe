#include "network/WebSocketServer.h"

#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <utility>

namespace pulse {
namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using tcp = net::ip::tcp;

}  // namespace

// ---------------------------------------------------------------- Session

// 하나의 WebSocket 연결. 쓰기는 한 번에 하나만 진행할 수 있으므로,
// 진행 중에 새 메시지가 오면 대기 슬롯 하나에 덮어쓴다.
class WebSocketServer::Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, WebSocketServer& server)
        : ws_(std::move(socket)), server_(server) {}

    void run(std::shared_ptr<const std::string> hello,
             std::shared_ptr<const std::string> latest) {
        pending_ = std::move(latest);
        first_ = std::move(hello);

        // 업그레이드 요청을 직접 읽어 Origin 을 검사한 뒤 수락한다.
        // Beast 의 async_accept 에는 거절 훅이 없다.
        http::async_read(ws_.next_layer(), buffer_, request_,
                         [self = shared_from_this()](beast::error_code ec, std::size_t) {
                             self->onRequest(ec);
                         });
    }

    void send(std::shared_ptr<const std::string> message) {
        if (!open_) {
            return;
        }
        if (writing_) {
            // 큐를 키우지 않는다. 밀린 스냅샷은 보낼 가치가 없다.
            pending_ = std::move(message);
            return;
        }
        sending_ = std::move(message);
        doWrite();
    }

    void close() {
        open_ = false;
        beast::error_code ignored;
        ws_.next_layer().close(ignored);
    }

private:
    void onRequest(beast::error_code ec) {
        if (ec) {
            server_.removeSession(shared_from_this());
            return;
        }

        if (!originAllowed()) {
            auto response = std::make_shared<http::response<http::string_body>>(
                http::status::forbidden, request_.version());
            response->set(http::field::content_type, "text/plain");
            response->body() = "origin not allowed";
            response->prepare_payload();
            http::async_write(ws_.next_layer(), *response,
                              [self = shared_from_this(), response](beast::error_code,
                                                                    std::size_t) {
                                  self->server_.removeSession(self);
                              });
            return;
        }

        ws_.async_accept(request_, [self = shared_from_this()](beast::error_code accept_ec) {
            self->onAccept(accept_ec);
        });
    }

    bool originAllowed() const {
        const auto it = request_.find(http::field::origin);
        if (it == request_.end()) {
            // Origin 이 없는 연결은 브라우저발이 아니다. 127.0.0.1 바인딩이 통제한다.
            return true;
        }
        const std::string origin(it->value());
        const auto& allowed = server_.cfg_.allowed_origins;
        return std::find(allowed.begin(), allowed.end(), origin) != allowed.end();
    }

    void onAccept(beast::error_code ec) {
        if (ec) {
            server_.removeSession(shared_from_this());
            return;
        }
        open_ = true;
        ws_.text(true);
        doRead();

        if (first_) {
            sending_ = std::move(first_);
            doWrite();
        } else if (pending_) {
            sending_ = std::move(pending_);
            doWrite();
        }
    }

    // 계약은 단방향이다. 읽기는 close 프레임과 연결 종료를 감지하기 위해서만 한다.
    void doRead() {
        read_buffer_.clear();
        ws_.async_read(read_buffer_,
                       [self = shared_from_this()](beast::error_code ec, std::size_t) {
                           if (ec) {
                               self->open_ = false;
                               self->server_.removeSession(self);
                               return;
                           }
                           self->doRead();
                       });
    }

    void doWrite() {
        writing_ = true;
        ws_.async_write(net::buffer(*sending_),
                        [self = shared_from_this()](beast::error_code ec, std::size_t) {
                            self->onWrite(ec);
                        });
    }

    void onWrite(beast::error_code ec) {
        writing_ = false;
        sending_.reset();

        if (ec) {
            open_ = false;
            server_.removeSession(shared_from_this());
            return;
        }
        if (pending_) {
            sending_ = std::move(pending_);
            doWrite();
        }
    }

    websocket::stream<tcp::socket> ws_;
    WebSocketServer& server_;
    beast::flat_buffer buffer_;
    beast::flat_buffer read_buffer_;
    http::request<http::string_body> request_;
    std::shared_ptr<const std::string> first_;
    std::shared_ptr<const std::string> sending_;
    std::shared_ptr<const std::string> pending_;
    bool writing_ = false;
    bool open_ = false;
};

// ---------------------------------------------------------- WebSocketServer

WebSocketServer::WebSocketServer(net::io_context& ioc, ServerConfig cfg)
    : ioc_(ioc), cfg_(std::move(cfg)), acceptor_(ioc) {
    // 루프백 전용. 0.0.0.0 바인딩 경로를 두지 않는다 — 계약서 4.2 절.
    const tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), cfg_.port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);

    doAccept();
}

WebSocketServer::~WebSocketServer() = default;

unsigned short WebSocketServer::port() const {
    return acceptor_.local_endpoint().port();
}

void WebSocketServer::setHello(std::shared_ptr<const std::string> hello) {
    net::post(ioc_, [this, hello = std::move(hello)]() mutable {
        hello_ = std::move(hello);
    });
}

void WebSocketServer::broadcast(std::shared_ptr<const std::string> message) {
    net::post(ioc_, [this, message = std::move(message)]() mutable {
        latest_ = message;
        for (const auto& session : sessions_) {
            session->send(message);
        }
    });
}

void WebSocketServer::stop() {
    net::post(ioc_, [this] {
        if (stopped_) {
            return;
        }
        stopped_ = true;

        beast::error_code ignored;
        acceptor_.close(ignored);
        for (const auto& session : sessions_) {
            session->close();
        }
        sessions_.clear();
    });
}

void WebSocketServer::doAccept() {
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        onAccept(ec, std::move(socket));
    });
}

void WebSocketServer::onAccept(boost::system::error_code ec, tcp::socket socket) {
    if (stopped_) {
        return;
    }
    if (!ec) {
        auto session = std::make_shared<Session>(std::move(socket), *this);
        sessions_.push_back(session);
        session->run(hello_, latest_);
    }
    doAccept();
}

void WebSocketServer::removeSession(const std::shared_ptr<Session>& session) {
    const auto it = std::find(sessions_.begin(), sessions_.end(), session);
    if (it != sessions_.end()) {
        sessions_.erase(it);
    }
}

}  // namespace pulse
