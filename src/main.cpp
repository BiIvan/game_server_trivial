#include <thread>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <algorithm>

#include "sdk.h"
#include "json_loader.h"
#include "http_server.h"
#include "request_handler.h"

using namespace std::literals;

namespace net = boost::asio;
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
    std::cerr << "Usage: game_server <config-file>"sv << std::endl;
    return EXIT_FAILURE;
  }

  try {
    model::Game game = json_loader::LoadGame(argv[1]);
    const unsigned num_threads = std::thread::hardware_concurrency();
    net::io_context ioc{static_cast<int>(std::max(1u, num_threads))};
    g_ioc = &ioc;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    http_handler::RequestHandler handler{game};
    const auto address = net::ip::make_address("0.0.0.0");
    constexpr unsigned short port = 8080;
    http_server::ServeHttp(ioc,tcp::endpoint{address, port},handler);
    std::cout << "Server has started..."sv << std::endl;
    RunWorkers(std::max(1u, num_threads), [&ioc] {ioc.run();});
    g_ioc = nullptr;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
}