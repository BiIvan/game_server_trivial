#pragma once

#include <chrono>
#include <memory>
#include <utility>
#include <iostream>
#include <optional>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/bind_handler.hpp>

#include "sdk.h"
#include "logger.h"

namespace http_server {

  namespace net = boost::asio;
  namespace beast = boost::beast;
  namespace http = beast::http;
  using tcp = net::ip::tcp;

  template <typename RequestHandler>
  class HttpSession
    : public std::enable_shared_from_this<HttpSession<RequestHandler>> {

    class SendLambda {
      std::shared_ptr<HttpSession> session_;

    public:
      explicit SendLambda(std::shared_ptr<HttpSession> session) noexcept
          : session_(std::move(session)) {
      }

      template <typename Response>
      void operator()(Response&& response) const {
        using ResponseType = std::decay_t<Response>;
        auto response_ptr = std::make_shared<ResponseType>(
          std::forward<Response>(response));
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = session_->request_time_
          ? std::chrono::duration_cast<std::chrono::milliseconds>(
            now - *session_->request_time_)
            .count()
          : 0;
        json::value content_type = nullptr;
        const auto content_type_it =
          response_ptr->find(http::field::content_type);
        if (content_type_it != response_ptr->end()) {
          const auto content_type_value =
            content_type_it->value();
          content_type = std::string{
            content_type_value.data(),
            content_type_value.size()
          };
        }
        BOOST_LOG_TRIVIAL(info)
          << logging::add_value(
            additional_data,
            json::object{
              {"response_time", elapsed},
              {"code", response_ptr->result_int()},
              {"content_type", std::move(content_type)},
            })
          << "response sent";
        session_->response_ = response_ptr;
        http::async_write(
          session_->stream_,
          *response_ptr,
          beast::bind_front_handler(
            &HttpSession::OnWrite,
            session_,
            response_ptr->need_eof()
          )
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
        logger::LogError(ec, "read");
        return;
      }
      request_time_ = std::chrono::steady_clock::now();
      BOOST_LOG_TRIVIAL(info)
        << logging::add_value(
          additional_data,
          json::object{
            {
              "ip",
              stream_.socket()
                .remote_endpoint()
                .address()
                .to_string()
            },
            {
              "URI",
              std::string{
                request_.target().data(),
                request_.target().size()
              }
            },
            {
              "method",
              std::string{
                request_.method_string().data(),
                request_.method_string().size()
              }
            },
          })
        << "request received";
      request_handler_(
        std::move(request_),
        SendLambda{this->shared_from_this()}
      );
    }

    void OnWrite(bool close, beast::error_code ec, std::size_t) {
      if (ec) {
        logger::LogError(ec, "write");
        return;
      }
      if (close) {
        DoClose();
        return;
      }
      response_.reset();
      request_time_.reset();
      client_endpoint_.reset();
      Read();
    }

    void DoClose() {
      beast::error_code ec;
      stream_.socket().shutdown(
        tcp::socket::shutdown_send,
        ec);
      if (ec && ec != beast::errc::not_connected) {
        logger::LogError(ec, "write");
      }
    }

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    RequestHandler& request_handler_;
    std::shared_ptr<void> response_;
    std::optional<std::chrono::steady_clock::time_point> request_time_;
    std::optional<tcp::endpoint> client_endpoint_;

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
  class Listener  : public std::enable_shared_from_this<Listener<RequestHandler>> {
    using RH = RequestHandler;
    void DoAccept() {
      acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&Listener::OnAccept,this->shared_from_this())
      );
    }

    void OnAccept(beast::error_code ec, tcp::socket socket) {
      if (ec) {
        logger::LogError(ec, "accept");
      } else {
        std::make_shared<HttpSession<RH>>(
          std::move(socket),
          request_handler_)
          ->Run();
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

