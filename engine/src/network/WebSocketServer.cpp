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
        if (!open_ || closing_) {
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
        if (closing_) {
            return;
        }
        closing_ = true;

        // 핸드셰이크 전에 죽은 연결은 보낼 것이 없다. 소켓만 닫는다.
        if (!open_) {
            beast::error_code ignored;
            ws_.next_layer().close(ignored);
            return;
        }

        // 큐에 남은 스냅샷은 버린다. 닫기로 한 뒤에 보낼 이유가 없다.
        pending_.reset();

        // async_close 는 나가는 연산이라 진행 중인 async_write 와 충돌한다.
        // 쓰기가 끝나면 onWrite 가 이어서 닫는다.
        if (writing_) {
            return;
        }
        doClose();
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

        // 핸드셰이크 타임아웃, 유휴 타임아웃, 자동 ping 을 한 번에 켠다.
        // 이게 없으면 절전된 탭이나 잠든 노트북의 세션이 무기한 남는다.
        ws_.set_option(
            websocket::stream_base::timeout::suggested(beast::role_type::server));
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

    void doClose() {
        open_ = false;
        ws_.async_close(websocket::close_code::normal,
                        [self = shared_from_this()](beast::error_code) {
                            // 실패해도 할 일이 없다. 세션은 이미 목록에서 빠진다.
                            beast::error_code ignored;
                            self->ws_.next_layer().close(ignored);
                        });
    }

    void onWrite(beast::error_code ec) {
        writing_ = false;
        sending_.reset();

        if (ec) {
            // 쓰기가 실패했다. 닫기를 미뤄둔 상태였을 수도 있는데, 여기서
            // 그냥 돌아가면 async_close 도 소켓 닫기도 일어나지 않는다.
            // 그러면 남아 있는 async_read 가 끝나리라는 보장이 없고,
            // io_context 가 비지 않아 종료가 멈춘다. 소켓을 확실히 닫는다.
            open_ = false;
            closing_ = true;
            beast::error_code ignored;
            ws_.next_layer().close(ignored);
            server_.removeSession(shared_from_this());
            return;
        }
        if (closing_) {
            doClose();
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
    bool closing_ = false;
};

// ---------------------------------------------------------- WebSocketServer

WebSocketServer::WebSocketServer(net::io_context& ioc, ServerConfig cfg)
    : ioc_(ioc), cfg_(std::move(cfg)), acceptor_(ioc) {
    // 루프백 전용. 0.0.0.0 바인딩 경로를 두지 않는다 — 계약서 4.2 절.
    const tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), cfg_.port);

    acceptor_.open(endpoint.protocol());
    // SO_REUSEADDR 을 설정하지 않는다. Windows 에서는 이 옵션이 다른 프로세스가
    // 이미 듣고 있는 주소에도 바인딩을 허용해, 두 번째 인스턴스가 조용히 포트를
    // 가로챈다. 이 엔진은 시스템 전역 프로세스 정보를 내보내므로 그 통제가
    // 루프백 바인딩에 달려 있다. 기본 동작이 중복 바인딩을 거부한다.
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);

    // 자기 자신의 origin 을 허용한다. M3 가 프론트엔드를 이 엔진에서 서빙하면
    // 브라우저가 보내는 Origin 이 바로 이 주소가 되는데, 그때 거절당하면
    // 계약서가 말하는 same-origin 배포가 기본값에서 막힌다.
    const std::string own = std::to_string(acceptor_.local_endpoint().port());
    cfg_.allowed_origins.push_back("http://127.0.0.1:" + own);
    cfg_.allowed_origins.push_back("http://localhost:" + own);

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
