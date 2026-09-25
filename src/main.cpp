#include <memory>
#include <thread>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>

#include "logger.h"
#include "http_server.h"
#include "json_loader.h"
#include "request_handler.h"

namespace fs = std::filesystem;
namespace net = boost::asio;
namespace json = boost::json;

using namespace std::literals;
using tcp = net::ip::tcp;

namespace {

  net::io_context* g_ioc = nullptr;

  void HandleSignal(int) {
    if (g_ioc != nullptr) {
      g_ioc->stop();
    }
  }

  template <typename Fn>
  void RunWorkers(unsigned n, const Fn& fn) {
    n = std::max(1u, n);
    std::vector<std::thread> workers;
    workers.reserve(n - 1);
    while (--n) {
      workers.emplace_back(fn);
    }
    fn();
    for (std::thread& worker : workers) {
      worker.join();
    }
  }

}  // namespace

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: "sv << argv[0] << " <config-file>\n"sv;
    return EXIT_FAILURE;
  }
  logger::InitLogger();
  try {
    const fs::path config_path = argv[1];
    const fs::path static_root =
      config_path.parent_path().parent_path() / "static";
    model::Game game = json_loader::LoadGame(config_path.string());
    const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
    net::io_context ioc{ static_cast<int>(num_threads) };
    g_ioc = &ioc;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    auto api_strand = net::make_strand(ioc);
    http_handler::RequestHandler handler{ game, static_root, std::move(api_strand)};
    const auto address = net::ip::make_address("0.0.0.0");
    constexpr unsigned short port = 8080;
    http_server::ServeHttp( ioc, tcp::endpoint{address, port}, handler);
    BOOST_LOG_TRIVIAL(info)
      << logging::add_value(
        additional_data, json::object{ {"port", port}, {"address", address.to_string()}, }
      )
      << "server started";
    logging::core::get()->flush();
    std::cout.flush();
    RunWorkers(num_threads, [&ioc] { ioc.run(); });
    g_ioc = nullptr;
    BOOST_LOG_TRIVIAL(info)
      << logging::add_value(
          additional_data, json::object{ {"code", EXIT_SUCCESS}, }
        )
      << "server exited";
    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    g_ioc = nullptr;
    BOOST_LOG_TRIVIAL(error)
      << logging::add_value(
          additional_data, json::object{ {"code", EXIT_FAILURE}, {"exception", ex.what()}, }
        )
      << "server exited";
    return EXIT_FAILURE;
  }
}