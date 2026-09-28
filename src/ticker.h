#pragma once

#include <memory>
#include <chrono>
#include <cassert>
#include <utility>
#include <exception>
#include <functional>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/bind_executor.hpp>

#include "logger.h"

using Clock = std::chrono::steady_clock;
using Strand = net::strand<net::io_context::executor_type>;
using Handler = std::function<void(std::chrono::milliseconds)>;
  
namespace net = boost::asio;

class Ticker : public std::enable_shared_from_this<Ticker> {
  using Clock = std::chrono::steady_clock;
  using Strand = net::strand<net::io_context::executor_type>;
  using Handler = std::function<void(std::chrono::milliseconds)>;

  void ScheduleTick() {
    assert(strand_.running_in_this_thread());
    timer_.expires_after(period_);
    timer_.async_wait(
      net::bind_executor(
        strand_,
        [self = shared_from_this()](
          const boost::system::error_code& ec) {
          self->OnTick(ec);
        }));
  }

  void OnTick(const boost::system::error_code& ec) {
    assert(strand_.running_in_this_thread());
    if (ec) {
      return;
    }
    const auto now = Clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_tick_);
    last_tick_ = now;
    try {
      handler_(delta);
    } catch (const std::exception& ex) {
      BOOST_LOG_TRIVIAL(error)
        << logging::add_value(
          additional_data,
          json::object{
            {"where", "Ticker::OnTick"},
            {"exception", ex.what()}
          })
        << "game tick failed";
    } catch (...) {
      BOOST_LOG_TRIVIAL(error)
        << logging::add_value(
          additional_data,
          json::object{
            {"where", "Ticker::OnTick"},
            {"exception", "unknown exception"}
          })
        << "game tick failed";
    }
    ScheduleTick();
  }

  Strand strand_;
  std::chrono::milliseconds period_;
  net::steady_timer timer_;
  Handler handler_;
  Clock::time_point last_tick_;
  
public:
  Ticker(
    Strand strand,
    std::chrono::milliseconds period,
    Handler handler)
    : strand_(std::move(strand))
    , period_(period)
    , timer_(strand_)
    , handler_(std::move(handler)) {
  }

  void Start() {
    net::dispatch(
      strand_,
      [self = shared_from_this()] {
        self->last_tick_ = Clock::now();
        self->ScheduleTick();
      });
  }
};