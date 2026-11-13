/*
 * Ambitus
 *
 * Unit tests for the reading of the handwriting through Summa's bridge.
 *
 * @license GNU GPLv2 or later
 */

#include <string>
#include <thread>

#include <gio/gio.h>
#include <glib.h>
#include <gtest/gtest.h>

#include "library/InkReader.h"

#include "filesystem.h"

using namespace ambitus::library;

TEST(InkReader, theBridgeFileIsInSummasRuntimeFolder) {
    const fs::path config = "/home/u/.config/Mesa de Estudos/config.json";
    EXPECT_EQ(summaBridgePath(config, "", nullptr),
              fs::path("/home/u/.config/Mesa de Estudos/runtime/ambitus-bridge.json"));
    EXPECT_EQ(summaBridgePath(config, R"({"runtimePath": "/dados/rt"})", nullptr),
              fs::path("/dados/rt/ambitus-bridge.json"));
    EXPECT_EQ(summaBridgePath(config, R"({"runtimePath": ""})", nullptr),
              fs::path("/home/u/.config/Mesa de Estudos/runtime/ambitus-bridge.json"));
    EXPECT_EQ(summaBridgePath(config, "{quebrado", nullptr),
              fs::path("/home/u/.config/Mesa de Estudos/runtime/ambitus-bridge.json"));
    // Summa honours $LEARNING_DESK_RUNTIME for both, and so does Ambitus
    EXPECT_EQ(summaBridgePath(config, R"({"runtimePath": "/dados/rt"})", "/tmp/rt"),
              fs::path("/tmp/rt/ambitus-bridge.json"));
}

TEST(InkReader, onlyABridgeOfAKnownVersionIsUsed) {
    auto bridge = parseSummaBridge(R"({"version": 1, "port": 40123, "token": "abc", "pid": 7})");
    ASSERT_TRUE(bridge);
    EXPECT_EQ(bridge->port, 40123);
    EXPECT_EQ(bridge->token, "abc");
    EXPECT_FALSE(parseSummaBridge(R"({"version": 2, "port": 40123, "token": "abc"})"));
    EXPECT_FALSE(parseSummaBridge(R"({"version": 1, "port": 0, "token": "abc"})"));
    EXPECT_FALSE(parseSummaBridge(R"({"version": 1, "port": 70000, "token": "abc"})"));
    EXPECT_FALSE(parseSummaBridge(R"({"version": 1, "port": 40123, "token": ""})"));
    EXPECT_FALSE(parseSummaBridge(R"({"version": 1, "port": 40123, "token": "a\r\nX-Evil: 1"})"));
    EXPECT_FALSE(parseSummaBridge("não é json"));
}

TEST(InkReader, theAnswerOfTheBridgeSaysWhatToDo) {
    auto read = parseInkReply(200, R"({"text": "Teorema do confronto"})");
    EXPECT_EQ(read.stop, InkStop::None);
    EXPECT_EQ(read.text, "Teorema do confronto");
    EXPECT_EQ(parseInkReply(200, R"({"text": ""})").stop, InkStop::None);  // nothing legible is an answer too
    EXPECT_EQ(parseInkReply(200, R"({"reason": "disabled"})").stop, InkStop::Disabled);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "no-vision"})").stop, InkStop::NoVision);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "free"})").stop, InkStop::Free);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "claude-unavailable"})").stop, InkStop::Claude);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "quota"})").stop, InkStop::Quota);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "busy"})").stop, InkStop::Busy);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "timeout"})").stop, InkStop::Failed);
    EXPECT_EQ(parseInkReply(200, R"({"reason": "algo-novo"})").stop, InkStop::Failed);
    EXPECT_EQ(parseInkReply(403, R"({"reason": "forbidden"})").stop, InkStop::Failed);
    EXPECT_EQ(parseInkReply(200, "lixo").stop, InkStop::Failed);
}

namespace {
/// A bridge of one connection: keeps the request, answers `response`
struct FakeBridge {
    GSocketListener* listener = g_socket_listener_new();
    int port = 0;
    std::string request;
    std::thread thread;

    explicit FakeBridge(std::string response) {
        port = g_socket_listener_add_any_inet_port(listener, nullptr, nullptr);
        thread = std::thread([this, response = std::move(response)] {
            GSocketConnection* conn = g_socket_listener_accept(listener, nullptr, nullptr, nullptr);
            if (!conn) {
                return;
            }
            GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
            char buf[4096];
            // the head, then as many body bytes as Content-Length says
            for (;;) {
                gssize n = g_input_stream_read(in, buf, sizeof(buf), nullptr, nullptr);
                if (n <= 0) {
                    break;
                }
                request.append(buf, static_cast<size_t>(n));
                auto headEnd = request.find("\r\n\r\n");
                auto lenAt = request.find("Content-Length: ");
                if (headEnd != std::string::npos && lenAt != std::string::npos &&
                    request.size() >= headEnd + 4 + std::stoul(request.substr(lenAt + 16))) {
                    break;
                }
            }
            GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
            g_output_stream_write_all(out, response.data(), response.size(), nullptr, nullptr, nullptr);
            g_io_stream_close(G_IO_STREAM(conn), nullptr, nullptr);
            g_object_unref(conn);
        });
    }
    ~FakeBridge() {
        thread.join();
        g_socket_listener_close(listener);
        g_object_unref(listener);
    }
};
}  // namespace

TEST(InkReader, thePageGoesWithTheTokenAndTheTextComesBack) {
    std::string answer;
    {
        const std::string body = R"({"text": "∫ x dx"})";
        FakeBridge fake("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\n\r\n" + body);
        auto reply =
                postInk({fake.port, "segredo"}, std::string("\x89PNG\r\n\x1a\n-bytes", 14), std::chrono::seconds(5));
        EXPECT_EQ(reply.stop, InkStop::None);
        answer = reply.text;
        fake.thread.join();
        fake.thread = std::thread([] {});
        EXPECT_NE(fake.request.find("POST /ler-tinta HTTP/1.0\r\n"), std::string::npos);
        EXPECT_NE(fake.request.find("Host: 127.0.0.1:" + std::to_string(fake.port) + "\r\n"), std::string::npos);
        EXPECT_NE(fake.request.find("x-summa-token: segredo\r\n"), std::string::npos);
        EXPECT_NE(fake.request.find("Content-Type: image/png\r\n"), std::string::npos);
        EXPECT_EQ(fake.request.substr(fake.request.size() - 14), std::string("\x89PNG\r\n\x1a\n-bytes", 14));
    }
    EXPECT_EQ(answer, "∫ x dx");
}

TEST(InkReader, aChunkedAnswerIsReadToo) {
    FakeBridge fake("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "a\r\n{\"reason\":\r\n9\r\n \"quota\"}\r\n0\r\n\r\n");
    EXPECT_EQ(postInk({fake.port, "t"}, "png", std::chrono::seconds(5)).stop, InkStop::Quota);
}

TEST(InkReader, withNobodyListeningSummaIsClosed) {
    int port = 0;
    {
        FakeBridge fake("");  // takes a free port, then closes it
        port = fake.port;
        GSocketClient* c = g_socket_client_new();
        g_object_unref(g_socket_client_connect_to_host(c, "127.0.0.1", static_cast<guint16>(port), nullptr, nullptr));
        g_object_unref(c);
    }
    EXPECT_EQ(postInk({port, "t"}, "png", std::chrono::seconds(5)).stop, InkStop::NoSumma);
}
