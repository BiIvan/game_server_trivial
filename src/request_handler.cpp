#include <cctype>
#include <string>
#include <string_view>

#include "request_handler.h"

namespace http_handler {
  
  RequestHandler::FileRequestResult RequestHandler::HandleFileRequest(
    const StringRequest& req) const {
    if (req.method() != http::verb::get &&
      req.method() != http::verb::head) {
      auto response = MakeTextResponse(
        http::status::method_not_allowed,
        req.version(),
        req.keep_alive(),
        "Only GET and HEAD methods are supported\n");
      response.set(http::field::allow, "GET, HEAD");
      return response;
    }
    const beast::string_view target = req.target();
    if (target.empty() || target.front() != '/') {
      return MakeTextResponse(
        http::status::bad_request,
        req.version(),
        req.keep_alive(),
        "Invalid request target\n");
    }
    const size_t query_position = target.find('?');
    const beast::string_view encoded_path =
      target.substr(0, query_position);
    const std::optional<std::string> decoded_path =
      UrlDecode(encoded_path);
    if (!decoded_path) {
      return MakeTextResponse(
        http::status::bad_request,
        req.version(),
        req.keep_alive(),
        "Invalid URL encoding\n");
    }
    std::string relative_path = *decoded_path;
    if (relative_path == "/") {
      relative_path = "/index.html";
    }
    relative_path.erase(0, 1);
    fs::path requested_path =
      fs::weakly_canonical(static_root_ / relative_path);
    if (!IsSubPath(requested_path, static_root_)) {
      return MakeTextResponse(
        http::status::bad_request,
        req.version(),
        req.keep_alive(),
        "Requested path is outside static directory\n");
    }
    boost::system::error_code ec;
    if (fs::is_directory(requested_path, ec)) {
      requested_path /= "index.html";
      requested_path = fs::weakly_canonical(requested_path);
    }
    if (ec) {
      return MakeTextResponse(
        http::status::not_found,
        req.version(),
        req.keep_alive(),
        "File not found\n");
    }
    if (!IsSubPath(requested_path, static_root_)) {
      return MakeTextResponse(
        http::status::bad_request,
        req.version(),
        req.keep_alive(),
        "Requested path is outside static directory\n");
    }
    http::file_body::value_type file;
    file.open(
      requested_path.string().c_str(),
      beast::file_mode::read,
      ec);
    if (ec == beast::errc::no_such_file_or_directory) {
      return MakeTextResponse(
        http::status::not_found,
        req.version(),
        req.keep_alive(),
        "File not found\n");
    }
    if (ec) {
      return MakeTextResponse(
        http::status::internal_server_error,
        req.version(),
        req.keep_alive(),
        "Failed to open file\n");
    }
    const auto file_size = file.size();
    if (req.method() == http::verb::head) {
      EmptyResponse response{
        http::status::ok,
        req.version()
      };
      response.set(
        http::field::content_type,
        GetMimeType(requested_path));
      response.content_length(file_size);
      response.keep_alive(req.keep_alive());
      return response;
    }
    FileResponse response{
      std::piecewise_construct,
      std::make_tuple(std::move(file)),
      std::make_tuple(
        http::status::ok,
        req.version())
    };
    response.set(
      http::field::content_type,
      GetMimeType(requested_path));
    response.content_length(file_size);
    response.keep_alive(req.keep_alive());
    return response;
  }
  
  RequestHandler::StringResponse RequestHandler::ReportServerError(
    unsigned version,
    bool keep_alive) const {
    return MakeErrorResponse(
      http::status::internal_server_error,
      version,
      keep_alive,
      "internalError",
      "Internal server error");
  }
  
  RequestHandler::StringResponse RequestHandler::HandleApiRequest(
    const StringRequest& request) {
    const std::string_view target{
      request.target().data(),
      request.target().size()
    };
    if (target == "/api/v1/game/join") {
      return HandleJoinGameRequest(request);
    }
    if (target == "/api/v1/game/players") {
      return HandlePlayersRequest(request);
    }
    if (target == "/api/v1/maps") {
     if (request.method() != http::verb::get &&
       request.method() != http::verb::head) {
      return MakeErrorResponse(
       http::status::bad_request,
       request.version(),
       request.keep_alive(),
       "badRequest",
       "Bad request");
     }
      return MakeMapsResponse(
        request.version(),
        request.keep_alive());
    }
    constexpr std::string_view kMapsPrefix = "/api/v1/maps/";
    if (target.starts_with(kMapsPrefix)) {
     if (request.method() != http::verb::get &&
       request.method() != http::verb::head) {
      return MakeErrorResponse(
       http::status::bad_request,
       request.version(),
       request.keep_alive(),
       "badRequest",
       "Bad request");
     }
      const std::string_view map_id =
        target.substr(kMapsPrefix.size());
      if (map_id.empty() ||
        map_id.find('/') != std::string_view::npos) {
        return MakeErrorResponse(
          http::status::bad_request,
          request.version(),
          request.keep_alive(),
          "badRequest",
          "Bad request");
      }
      const model::Map* map = game_.FindMap(
        model::Map::Id{std::string(map_id)});
      if (map == nullptr) {
        return MakeErrorResponse(
          http::status::not_found,
          request.version(),
          request.keep_alive(),
          "mapNotFound",
          "Map not found");
      }
      return MakeMapResponse(
        *map,
        request.version(),
        request.keep_alive());
    }
    return MakeErrorResponse(
      http::status::bad_request,
      request.version(),
      request.keep_alive(),
      "badRequest",
      "Bad request");
  }
  
  RequestHandler::StringResponse RequestHandler::HandleJoinGameRequest(
    const StringRequest& request) {
    if (request.method() != http::verb::post) {
      auto response = MakeErrorResponse(
        http::status::method_not_allowed,
        request.version(),
        request.keep_alive(),
        "invalidMethod",
        "Only POST method is expected");
      response.set(http::field::allow, "POST");
      return response;
    }
    if (!IsJsonContentType(request)) {
      return MakeErrorResponse(
        http::status::bad_request,
        request.version(),
        request.keep_alive(),
        "invalidArgument",
        "Invalid content type");
    }
    try {
      const json::value request_json = json::parse(request.body());
      const json::object& object = request_json.as_object();
      const std::string user_name =
        json::value_to<std::string>(
          object.at("userName"));
      const std::string map_id =
        json::value_to<std::string>(
          object.at("mapId"));
      if (user_name.empty()) {
        return MakeErrorResponse(
          http::status::bad_request,
          request.version(),
          request.keep_alive(),
          "invalidArgument",
          "Invalid name");
      }
      try {
        const app::Application::JoinResult result =
          app_.JoinGame(
            model::Map::Id{map_id},
            user_name);
        return MakeJsonResponse(
          http::status::ok,
          request.version(),
          request.keep_alive(),
          json::object{
            {"authToken", *result.token},
            {"playerId", *result.player_id},
          });
      } catch (const std::out_of_range&) {
        return MakeErrorResponse(
          http::status::not_found,
          request.version(),
          request.keep_alive(),
          "mapNotFound",
          "Map not found");
      }
    } catch (...) {
      return MakeErrorResponse(
        http::status::bad_request,
        request.version(),
        request.keep_alive(),
        "invalidArgument",
        "Join game request parse error");
    }
  }
  
  std::optional<model::Token> RequestHandler::ExtractBearerToken(
    const StringRequest& request) {
    constexpr std::string_view kBearerPrefix = "Bearer ";
    const auto authorization = request[http::field::authorization];
    const std::string_view value{ authorization.data(), authorization.size() };
    if (!value.starts_with(kBearerPrefix)) { return std::nullopt; }
    const std::string_view token = value.substr(kBearerPrefix.size());
    if (token.size() != 32) { return std::nullopt; }
    for (const unsigned char ch : token) {
      if (!std::isxdigit(ch)) { return std::nullopt; }
    }
    return model::Token{std::string(token)};
  }
  
  RequestHandler::StringResponse RequestHandler::HandlePlayersRequest(
    const StringRequest& request) {
    if (request.method() != http::verb::get &&
      request.method() != http::verb::head) {
      auto response = MakeErrorResponse(
        http::status::method_not_allowed,
        request.version(),
        request.keep_alive(),
        "invalidMethod",
        "Invalid method");
      response.set(http::field::allow, "GET, HEAD");
      return response;
    }
    const std::optional<model::Token> token =
      ExtractBearerToken(request);
    if (!token) {
      return MakeErrorResponse(
        http::status::unauthorized,
        request.version(),
        request.keep_alive(),
        "invalidToken",
        "Authorization header is missing");
    }
    const model::Player* current_player =
      app_.FindPlayerByToken(*token);
    if (current_player == nullptr) {
      return MakeErrorResponse(
        http::status::unauthorized,
        request.version(),
        request.keep_alive(),
        "unknownToken",
        "Player token has not been found");
    }
    const model::GameSession& current_session =
      current_player->GetSession();
    json::object players_json;
    for (const auto& [player_id, player] : app_.GetPlayers()) {
      if (&player.GetSession() != &current_session) { continue; }
      players_json.emplace(
        std::to_string(*player_id),
        json::object{ {"name", player.GetDog().GetName()} }
      );
    }
    StringResponse response = MakeJsonResponse(
      http::status::ok, request.version(),
      request.keep_alive(), std::move(players_json));
    if (request.method() == http::verb::head) {
      response.body().clear();
      response.content_length(0);
    }
    return response;
  }
} // namespace http_handler