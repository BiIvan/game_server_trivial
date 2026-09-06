#pragma once

#include <memory>
#include <chrono>
#include <utility>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/bind_handler.hpp>

#include "sdk.h"

namespace http_server {

  namespace net = boost::asio;
  namespace beast = boost::beast;
  namespace http = beast::http;
  using tcp = net::ip::tcp;

  template <typename RequestHandler>
  class HttpSession
    : public std::enable_shared_from_this<HttpSession<RequestHandler>> {
    class SendLambda {
      HttpSession& session_;
      
    public:
      explicit SendLambda(HttpSession& session) noexcept
        : session_(session) {
      }

      template <typename Response>
      void operator()(Response&& response) const {
        using ResponseType = std::decay_t<Response>;
        auto response_ptr = std::make_shared<ResponseType>(std::forward<Response>(response));
        session_.response_ = response_ptr;
        http::async_write(
          session_.stream_,
          *response_ptr,
          beast::bind_front_handler(
            &HttpSession::OnWrite,session_.shared_from_this(),
            response_ptr->need_eof())
        );
      }
    };

    void Read() {
      request_ = {};
      stream_.expires_after(std::chrono::seconds(30));
      http::async_read(
        stream_,
        buffer_,
        request_,
        beast::bind_front_handler(&HttpSession::OnRead,this->shared_from_this())
      );
    }

    void OnRead(beast::error_code ec, std::size_t) {
      if (ec == http::error::end_of_stream) {
        DoClose();
        return;
      }
      if (ec) {
        return;
      }
      request_handler_(std::move(request_), SendLambda{*this});
    }

    void OnWrite(bool close, beast::error_code ec, std::size_t) {
      if (ec) {
        return;
      }
      if (close) {
        DoClose();
        return;
      }
      response_ = nullptr;
      Read();
    }

    void DoClose() {
      beast::error_code ec;
      stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    }

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    RequestHandler& request_handler_;
    std::shared_ptr<void> response_;
      
  public:
    HttpSession(tcp::socket&& socket, RequestHandler& request_handler)
      : stream_(std::move(socket))
      , request_handler_(request_handler) {
    }

    void Run() {
      net::dispatch(
        stream_.get_executor(),
        beast::bind_front_handler(&HttpSession::Read,this->shared_from_this())
      );
    }
  };

  template <typename RequestHandler>
  class Listener
    : public std::enable_shared_from_this<Listener<RequestHandler>> {
    using RH = RequestHandler;
    void DoAccept() {
      acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&Listener::OnAccept,this->shared_from_this())
      );
    }

    void OnAccept(beast::error_code ec, tcp::socket socket) {
      if (!ec) {
        std::make_shared<HttpSession<RH>>(std::move(socket),request_handler_)->Run();
      }

      DoAccept();
    }
    
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    RH& request_handler_;
    
  public:
    Listener(net::io_context& ioc,
         tcp::endpoint endpoint,
         RequestHandler& request_handler)
      : ioc_(ioc)
      , acceptor_(net::make_strand(ioc))
      , request_handler_(request_handler) {
      beast::error_code ec;
      acceptor_.open(endpoint.protocol(), ec);
      if (ec) {
        throw beast::system_error(ec);
      }
      acceptor_.set_option(net::socket_base::reuse_address(true), ec);
      if (ec) {
        throw beast::system_error(ec);
      }
      acceptor_.bind(endpoint, ec);
      if (ec) {
        throw beast::system_error(ec);
      }
      acceptor_.listen(net::socket_base::max_listen_connections, ec);
      if (ec) {
        throw beast::system_error(ec);
      }
    }

    void Run() {
      DoAccept();
    }
  };

  template <typename RH>
  void ServeHttp(net::io_context& ioc, const tcp::endpoint& endpoint,RH& request_handler) {
    std::make_shared<Listener<RH>>(ioc,endpoint,request_handler)->Run();
  }

}  // namespace http_server