#include <thread>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>

#include "sdk.h"
#include "http_server.h"
#include "json_loader.h"
#include "request_handler.h"

namespace fs = std::filesystem;
namespace net = boost::asio;
namespace logging = boost::log;

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

void MyFormatter(logging::record_view const& rec,
  logging::formatting_ostream& strm) {
  strm << rec[logging::trivial::severity] << ": "
   << rec[logging::expressions::smessage];
}

void InitLogging() {
  logging::add_console_log(
  std::cout,
  logging::keywords::format = &MyFormatter,
  logging::keywords::auto_flush = true);
}

int main(int argc, const char* argv[]) {
  InitLogging();
  if (argc != 3) {
    std::cerr << "Usage: "sv << argv[0] << " <config-file> <static-root>\n"sv;
    return EXIT_FAILURE;
  }
  try {
    const fs::path config_path = argv[1];
    std::error_code fs_error;
    const fs::path static_root = fs::weakly_canonical(fs::absolute(argv[2], fs_error), fs_error);
    if (fs_error || !fs::is_directory(static_root, fs_error)) {
      std::cerr << "Static directory does not exist or is unavailable: "sv << argv[2] << '\n';
      return EXIT_FAILURE;
    }
    model::Game game = json_loader::LoadGame(config_path.string());
    const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
    net::io_context ioc{ static_cast<int>(num_threads) };
    g_ioc = &ioc;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    http_handler::RequestHandler handler{ game, static_root };
    const auto address = net::ip::make_address("0.0.0.0");
    constexpr unsigned short port = 8080;
    http_server::ServeHttp(ioc, tcp::endpoint{ address, port }, handler);
    std::cout << "Server has started on port " << port << std::endl;
    RunWorkers(num_threads, [&ioc] { ioc.run(); });
    g_ioc = nullptr;
  }
  catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}

