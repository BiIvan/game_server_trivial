#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include "logger.h"
#include "ticker.h"
#include "http_server.h"
#include "json_loader.h"
#include "request_handler.h"

namespace net = boost::asio;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace po = boost::program_options;

using tcp = net::ip::tcp;
using namespace std::literals;

namespace {

  struct Args {
    fs::path config_file;
    fs::path www_root;
    std::optional<std::chrono::milliseconds> tick_period;
    bool randomize_spawn_points = false;
  };

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
  
  std::optional<Args> ParseArgs(int argc, const char* const argv[]) {
    po::options_description desc{"Allowed options"};
    desc.add_options()
      ("help,h", "produce help message")
      ("tick-period,t",
        po::value<std::int64_t>()->value_name("milliseconds"),
        "set tick period")
      ("config-file,c",
        po::value<std::string>()->value_name("file"),
        "set config file path")
      ("www-root,w",
        po::value<std::string>()->value_name("dir"),
        "set static files root")
      ("randomize-spawn-points",
        "spawn dogs at random positions");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help")) {
      std::cout << desc << '\n';
      return std::nullopt;
    }
    po::notify(vm);
    if (!vm.count("config-file") || !vm.count("www-root")) {
      throw std::invalid_argument(
        "--config-file and --www-root are required");
    }
    Args args{
      .config_file = vm["config-file"].as<std::string>(),
      .www_root = vm["www-root"].as<std::string>(),
      .randomize_spawn_points =
        vm.count("randomize-spawn-points") != 0
    };
    if (vm.count("tick-period")) {
      const auto ms = vm["tick-period"].as<std::int64_t>();
      if (ms <= 0) {
        throw std::invalid_argument(
          "--tick-period must be greater than zero");
      }
      args.tick_period = std::chrono::milliseconds{ms};
    }
    return args;
  }

}  // namespace

int main(int argc, const char* argv[]) {
  try {
    const auto args = ParseArgs(argc, argv);
    if (!args) {
      return EXIT_SUCCESS;
    }
    logger::InitLogger();
    model::Game game = json_loader::LoadGame(args->config_file);
    const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
    net::io_context ioc{static_cast<int>(num_threads)};
    g_ioc = &ioc;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    auto api_strand = net::make_strand(ioc);
    http_handler::RequestHandler handler{
      game,
      args->www_root,
      api_strand,
      args->randomize_spawn_points,
      args->tick_period.has_value()
    };
    std::shared_ptr<Ticker> ticker;
    if (args->tick_period) {
      ticker = std::make_shared<Ticker>(
        api_strand,
        *args->tick_period,
        [&handler](std::chrono::milliseconds delta) {
          handler.GetApplication().Tick(delta.count());
        });
      ticker->Start();
    }
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