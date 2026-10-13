#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <utility>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include "logger.h"

class MovementTicker : public std::enable_shared_from_this<MovementTicker> {
public:
    using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
    using Interval = std::chrono::milliseconds;
    using Handler = std::function<void(Interval)>;

    MovementTicker(Strand strand, Interval period, Handler handler)
        : strand_(std::move(strand)), timer_(strand_), period_(period),
          handler_(std::move(handler)) {}

    void Start() {
        boost::asio::dispatch(strand_, [self = shared_from_this()] {
            self->last_ = std::chrono::steady_clock::now();
            self->Schedule();
        });
    }

private:
    void Schedule() {
        timer_.expires_after(period_);
        timer_.async_wait(boost::asio::bind_executor(strand_,
            [self = shared_from_this()](boost::system::error_code ec) {
                if (ec) { return; }
                const auto now = std::chrono::steady_clock::now();
                const auto delta = std::chrono::duration_cast<Interval>(now - self->last_);
                // Keep fractional milliseconds for the next update.
                self->last_ += delta;
                try {
                    self->handler_(delta);
                } catch (const std::exception& ex) {
                    BOOST_LOG_TRIVIAL(error) << "movement tick failed: " << ex.what();
                }
                self->Schedule();
            }));
    }
    Strand strand_;
    boost::asio::steady_timer timer_;
    Interval period_;
    Handler handler_;
    std::chrono::steady_clock::time_point last_;
};
