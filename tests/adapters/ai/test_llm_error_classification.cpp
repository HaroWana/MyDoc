#include <catch2/catch_test_macros.hpp>

#include "llm_client.hpp"
#include "llm_error.hpp"

#include <httplib.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

using namespace mondoc::adapters::ai;

TEST_CASE("LlmClient::classifyHttpStatus: 429 -> RateLimited",
          "[adapters.ai][llm_client][app06]") {
    auto err = LlmClient::classifyHttpStatus(429, "");
    REQUIRE(err.kind() == LlmError::Kind::RateLimited);
}

TEST_CASE("LlmClient::classifyHttpStatus: 500 -> BadResponse with status",
          "[adapters.ai][llm_client][app06]") {
    auto err = LlmClient::classifyHttpStatus(500, "");
    REQUIRE(err.kind() == LlmError::Kind::BadResponse);
    REQUIRE(err.message().find("500") != std::string::npos);
}

TEST_CASE("LlmClient::classifyHttpStatus: 503 -> BadResponse",
          "[adapters.ai][llm_client][app06]") {
    auto err = LlmClient::classifyHttpStatus(503, "");
    REQUIRE(err.kind() == LlmError::Kind::BadResponse);
    REQUIRE(err.message().find("503") != std::string::npos);
}

TEST_CASE("LlmClient::classifyHttpStatus: 401 -> BadResponse with status",
          "[adapters.ai][llm_client][app06]") {
    auto err = LlmClient::classifyHttpStatus(401, "unauthorized");
    REQUIRE(err.kind() == LlmError::Kind::BadResponse);
    REQUIRE(err.message().find("401") != std::string::npos);
}

TEST_CASE("LlmClient::classifyHttpStatus: 404 -> BadResponse",
          "[adapters.ai][llm_client][app06]") {
    auto err = LlmClient::classifyHttpStatus(404, "not found");
    REQUIRE(err.kind() == LlmError::Kind::BadResponse);
    REQUIRE(err.message().find("404") != std::string::npos);
}

TEST_CASE("LlmClient::isAcceptableHost: https accepted",
          "[adapters.ai][llm_client][app06]") {
    REQUIRE(LlmClient::isAcceptableHost("https://hub.example.com"));
}

TEST_CASE("LlmClient::isAcceptableHost: http non-localhost rejected",
          "[adapters.ai][llm_client][app06]") {
    REQUIRE_FALSE(LlmClient::isAcceptableHost("http://hub.example.com"));
}

TEST_CASE("LlmClient::isAcceptableHost: http localhost accepted",
          "[adapters.ai][llm_client][app06]") {
    REQUIRE(LlmClient::isAcceptableHost("http://localhost:8080"));
}

TEST_CASE("LlmClient::isAcceptableHost: http 127.0.0.1 accepted",
          "[adapters.ai][llm_client][app06]") {
    REQUIRE(LlmClient::isAcceptableHost("http://127.0.0.1:11434"));
}

TEST_CASE("LlmClient constructor: rejects non-HTTPS non-localhost host",
          "[adapters.ai][llm_client][app06]") {
    REQUIRE_THROWS_AS(LlmClient("http://hub.example.com", "k"),
                      std::invalid_argument);
}

TEST_CASE("LlmClient::chat: 429 from real server -> RateLimited (APP-06)",
          "[adapters.ai][llm_client][app06]") {
    httplib::Server svr;
    svr.Post("/v1/chat/completions",
             [](const httplib::Request&, httplib::Response& res) {
                 res.status = 429;
             });

    int port = svr.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        SKIP("could not bind test server");
    }
    std::thread t([&] { svr.listen_after_bind(); });
    while (!svr.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LlmClient client("http://127.0.0.1:" + std::to_string(port), "k");
    auto result = client.chat("{}");
    svr.stop();
    t.join();

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::RateLimited);
}

TEST_CASE("LlmClient::chat: 500 from real server -> BadResponse (APP-06)",
          "[adapters.ai][llm_client][app06]") {
    httplib::Server svr;
    svr.Post("/v1/chat/completions",
             [](const httplib::Request&, httplib::Response& res) {
                 res.status = 500;
                 res.set_content("internal error", "text/plain");
             });

    int port = svr.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        SKIP("could not bind test server");
    }
    std::thread t([&] { svr.listen_after_bind(); });
    while (!svr.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LlmClient client("http://127.0.0.1:" + std::to_string(port), "k");
    auto result = client.chat("{}");
    svr.stop();
    t.join();

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::BadResponse);
    REQUIRE(result.error().message().find("500") != std::string::npos);
}

TEST_CASE("LlmClient::chat: unreachable host -> Unreachable (APP-03)",
          "[adapters.ai][llm_client][app06]") {
    // Bind a server briefly to discover a free port, then stop it so the port
    // becomes refused. cpp-httplib's connect should then fail.
    httplib::Server probe;
    int port = probe.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        SKIP("could not bind probe server");
    }
    // Don't listen — let the OS reject connections.

    LlmClient client("http://127.0.0.1:" + std::to_string(port), "k");
    auto result = client.chat("{}");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == LlmError::Kind::Unreachable);
}
