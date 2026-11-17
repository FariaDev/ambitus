/*
 * Ambitus
 *
 * Unit tests for the reading of the handwriting through Summa's bridge.
 *
 * @license GNU GPLv2 or later
 */

#include <optional>
#include <string>
#include <thread>
#include <vector>

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

/// Answers each connection with the next response, and keeps the requests in order
struct ScriptedBridge {
    GSocketListener* listener = g_socket_listener_new();
    int port = 0;
    std::vector<std::string> requests;
    std::thread thread;

    explicit ScriptedBridge(std::vector<std::string> responses) {
        port = g_socket_listener_add_any_inet_port(listener, nullptr, nullptr);
        thread = std::thread([this, responses = std::move(responses)] {
            for (const auto& response: responses) {
                GSocketConnection* conn = g_socket_listener_accept(listener, nullptr, nullptr, nullptr);
                if (!conn) {
                    return;
                }
                GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
                std::string request;
                char buf[4096];
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
                requests.push_back(request);
                GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
                g_output_stream_write_all(out, response.data(), response.size(), nullptr, nullptr, nullptr);
                g_io_stream_close(G_IO_STREAM(conn), nullptr, nullptr);
                g_object_unref(conn);
            }
        });
    }
    ~ScriptedBridge() {
        if (thread.joinable()) {
            thread.join();
        }
        g_socket_listener_close(listener);
        g_object_unref(listener);
    }
};

/// The body of a request (what follows its blank line)
std::string bodyOf(const std::string& request) { return request.substr(request.find("\r\n\r\n") + 4); }
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

TEST(InkReader, theAnswerToASentPageSaysWhereItWent) {
    EXPECT_EQ(parsePageReply(200, R"({"ok": true})"), PageSend::Sent);
    EXPECT_EQ(parsePageReply(200, R"({"reason": "not-found"})"), PageSend::NotFound);
    EXPECT_EQ(parsePageReply(200, R"({"reason": "no-window"})"), PageSend::NoWindow);
    EXPECT_EQ(parsePageReply(200, R"({"reason": "unavailable"})"), PageSend::OldSumma);
    EXPECT_EQ(parsePageReply(404, R"({"error": "Rota desconhecida."})"), PageSend::OldSumma);  // a Summa from before
    EXPECT_EQ(parsePageReply(400, R"({"reason": "bad-request"})"), PageSend::Failed);
    EXPECT_EQ(parsePageReply(200, "lixo"), PageSend::Failed);
}

TEST(InkReader, aPageGoesToTheSideChatByItsPath) {
    FakeBridge fake("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}");
    EXPECT_EQ(sendPageToSumma({fake.port, "segredo"}, "/cadernos/Limites — aula 3.xopp", 1, "Limites — aula 3"),
              PageSend::Sent);
    fake.thread.join();
    fake.thread = std::thread([] {});
    EXPECT_NE(fake.request.find("POST /mandar-pagina HTTP/1.0\r\n"), std::string::npos);
    EXPECT_NE(fake.request.find("x-summa-token: segredo\r\n"), std::string::npos);
    EXPECT_NE(fake.request.find("Content-Type: application/json\r\n"), std::string::npos);
    const std::string body = fake.request.substr(fake.request.find("\r\n\r\n") + 4);
    EXPECT_EQ(body, R"({"caminho":"/cadernos/Limites — aula 3.xopp","pagina":2,"nome":"Limites — aula 3"})");
}

TEST(InkReader, aSentPageCarriesThePngOnlyWhenThereIsOne) {
    EXPECT_EQ(pageSendJson("/cadernos/Limites.xopp", 0, "Limites", std::string("\x89PNG", 4)),
              R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites","png":"iVBORw=="})");
    EXPECT_EQ(pageSendJson("/cadernos/Limites.xopp", 0, "Limites", ""),
              R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites"})");
}

TEST(InkReader, aPartSentGoesWithItsPlaceOnThePageAndOnlyWithItsPicture) {
    const PageCrop crop{88, 188, 74, 64};
    EXPECT_EQ(pageSendJson("/cadernos/Limites.xopp", 0, "Limites", std::string("\x89PNG", 4), crop),
              R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites",)"
              R"("recorte":{"x":88.0,"y":188.0,"largura":74.0,"altura":64.0},"png":"iVBORw=="})");
    // without a picture the place would be a lie: Summa reads the whole page from the file
    EXPECT_EQ(pageSendJson("/cadernos/Limites.xopp", 0, "Limites", "", crop),
              R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites"})");
    // a whole page has no place: the JSON is the one of before
    EXPECT_EQ(pageSendJson("/cadernos/Limites.xopp", 0, "Limites", std::string("\x89PNG", 4), std::nullopt),
              R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites","png":"iVBORw=="})");
}

TEST(InkReader, aPartOfAPageAnswerGoesWithItsPlaceToo) {
    EXPECT_EQ(pageAnswerJson("a1b2", "/cadernos/Limites.xopp", 1, "Limites", "abc", PageCrop{0, 10.5, 300, 200}),
              R"({"pedido":"a1b2","caminho":"/cadernos/Limites.xopp","pagina":2,"nome":"Limites",)"
              R"("recorte":{"x":0.0,"y":10.5,"largura":300.0,"altura":200.0},"png":"YWJj"})");
    EXPECT_EQ(pageAnswerJson("a1b2", "/cadernos/Limites.xopp", 1, "Limites", "abc"),
              R"({"pedido":"a1b2","caminho":"/cadernos/Limites.xopp","pagina":2,"nome":"Limites","png":"YWJj"})");
}

TEST(InkReader, aSentPartSaysItWasSentAsAPart) {
    FakeBridge fake("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}");
    bool cropSent = false;
    EXPECT_EQ(sendPageToSumma({fake.port, "segredo"}, "/cadernos/Limites.xopp", 0, "Limites", "\x89PNG",
                              PageCrop{88, 188, 74, 64}, &cropSent),
              PageSend::Sent);
    fake.thread.join();
    fake.thread = std::thread([] {});
    EXPECT_TRUE(cropSent);
    EXPECT_EQ(bodyOf(fake.request), R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites",)"
                                    R"("recorte":{"x":88.0,"y":188.0,"largura":74.0,"altura":64.0},"png":"iVBORw=="})");
}

TEST(InkReader, aSummaThatRefusesThePngGetsThePathAloneAndTheWholePage) {
    // an old Summa answers 400 to the larger body: the path goes again, without the picture and without its place
    ScriptedBridge fake({"HTTP/1.1 400 Bad Request\r\nContent-Length: 2\r\n\r\n{}",
                         "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}"});
    bool cropSent = true;
    EXPECT_EQ(sendPageToSumma({fake.port, "segredo"}, "/cadernos/Limites.xopp", 0, "Limites", "\x89PNG",
                              PageCrop{88, 188, 74, 64}, &cropSent),
              PageSend::Sent);
    fake.thread.join();
    fake.thread = std::thread([] {});
    ASSERT_EQ(fake.requests.size(), 2u);
    EXPECT_NE(bodyOf(fake.requests[0]).find("recorte"), std::string::npos);
    EXPECT_EQ(bodyOf(fake.requests[1]), R"({"caminho":"/cadernos/Limites.xopp","pagina":1,"nome":"Limites"})");
    EXPECT_FALSE(cropSent);
}

TEST(InkReader, theAnswerToTheLongPollSaysWhatSummaWants) {
    auto asked = parsePollReply(200, R"({"pedido": "a1b2c3d4e5f6"})");
    EXPECT_EQ(asked.kind, PollKind::Request);
    EXPECT_EQ(asked.id, "a1b2c3d4e5f6");
    EXPECT_EQ(parsePollReply(204, "").kind, PollKind::Idle);  // nothing asked in ~25 s: ask again
    EXPECT_EQ(parsePollReply(409, R"({"reason": "replaced"})").kind, PollKind::Failed);
    EXPECT_EQ(parsePollReply(404, R"({"error": "Rota desconhecida."})").kind, PollKind::Failed);  // a Summa from before
    EXPECT_EQ(parsePollReply(403, R"({"reason": "forbidden"})").kind, PollKind::Failed);
    EXPECT_EQ(parsePollReply(200, R"({"pedido": ""})").kind, PollKind::Failed);
    EXPECT_EQ(parsePollReply(200, R"({"outra": 1})").kind, PollKind::Failed);
    EXPECT_EQ(parsePollReply(200, "lixo").kind, PollKind::Failed);
}

TEST(InkReader, thePageAnswerCarriesTheRequestThePathThePageAndThePng) {
    // 1-based on the wire, the PNG in base64, the path empty for a notebook never saved
    EXPECT_EQ(pageAnswerJson("a1b2", "/cadernos/Limites.xopp", 1, "Limites", "abc"),
              R"({"pedido":"a1b2","caminho":"/cadernos/Limites.xopp","pagina":2,"nome":"Limites","png":"YWJj"})");
    EXPECT_EQ(pageAnswerJson("a1b2", "", 0, "", std::string("\x89PNG", 4)),
              R"({"pedido":"a1b2","caminho":"","pagina":1,"nome":"","png":"iVBORw=="})");
    EXPECT_EQ(noPageAnswerJson("a1b2"), R"({"pedido":"a1b2","erro":"sem-pagina"})");
}

TEST(InkReader, theLongPollWaitsLongerEachTimeSummaIsAway) {
    EXPECT_EQ(pollBackoff(0), std::chrono::seconds(2));
    EXPECT_EQ(pollBackoff(1), std::chrono::seconds(2));
    EXPECT_EQ(pollBackoff(2), std::chrono::seconds(5));
    EXPECT_EQ(pollBackoff(3), std::chrono::seconds(15));
    EXPECT_EQ(pollBackoff(4), std::chrono::seconds(30));
    EXPECT_EQ(pollBackoff(1000), std::chrono::seconds(30));
}

TEST(InkReader, theLongPollIsAGetWithTheTokenAndNoBody) {
    FakeBridge fake("HTTP/1.1 200 OK\r\nContent-Length: 25\r\n\r\n{\"pedido\":\"0123456789abcd\"}");
    auto reply = pollPageRequest({fake.port, "segredo"}, nullptr);
    EXPECT_EQ(reply.kind, PollKind::Request);
    EXPECT_EQ(reply.id, "0123456789abcd");
    fake.thread.join();
    fake.thread = std::thread([] {});
    EXPECT_NE(fake.request.find("GET /pedido-pagina HTTP/1.0\r\n"), std::string::npos);
    EXPECT_NE(fake.request.find("x-summa-token: segredo\r\n"), std::string::npos);
    EXPECT_EQ(fake.request.find("Content-Type"), std::string::npos);
}

TEST(InkReader, aQuietLongPollEndsIdleAndNobodyListeningIsNoSumma) {
    FakeBridge quiet("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(pollPageRequest({quiet.port, "t"}, nullptr).kind, PollKind::Idle);
    int port = 0;
    {
        FakeBridge gone("");
        port = gone.port;
        GSocketClient* c = g_socket_client_new();
        g_object_unref(g_socket_client_connect_to_host(c, "127.0.0.1", static_cast<guint16>(port), nullptr, nullptr));
        g_object_unref(c);
    }
    EXPECT_EQ(pollPageRequest({port, "t"}, nullptr).kind, PollKind::NoSumma);
}

TEST(InkReader, cancellingStopsALongPollSummaIsStillHolding) {
    // a Summa that takes the request and answers nothing for a while
    GSocketListener* listener = g_socket_listener_new();
    const int port = g_socket_listener_add_any_inet_port(listener, nullptr, nullptr);
    std::thread holder([listener] {
        GSocketConnection* conn = g_socket_listener_accept(listener, nullptr, nullptr, nullptr);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (conn) {
            g_io_stream_close(G_IO_STREAM(conn), nullptr, nullptr);
            g_object_unref(conn);
        }
    });
    GCancellable* cancel = g_cancellable_new();
    std::thread stopper([cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        g_cancellable_cancel(cancel);
    });
    const auto start = std::chrono::steady_clock::now();
    auto reply = pollPageRequest({port, "t"}, cancel);
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_NE(reply.kind, PollKind::Request);
    EXPECT_LT(took, std::chrono::milliseconds(1500));  // not the 2 s the holder waits, let alone the 40 s timeout
    stopper.join();
    holder.join();
    g_object_unref(cancel);
    g_socket_listener_close(listener);
    g_object_unref(listener);
}

TEST(InkReader, theAnswerPageIsPostedAsJsonAndSummaSaysIfItTookIt) {
    FakeBridge fake("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}");
    const std::string json = pageAnswerJson("a1b2", "", 0, "", "png");
    EXPECT_TRUE(postPageAnswer({fake.port, "segredo"}, json, nullptr));
    fake.thread.join();
    fake.thread = std::thread([] {});
    EXPECT_NE(fake.request.find("POST /pagina-atual HTTP/1.0\r\n"), std::string::npos);
    EXPECT_NE(fake.request.find("x-summa-token: segredo\r\n"), std::string::npos);
    EXPECT_NE(fake.request.find("Content-Type: application/json\r\n"), std::string::npos);
    EXPECT_EQ(fake.request.substr(fake.request.find("\r\n\r\n") + 4), json);

    FakeBridge late("HTTP/1.1 404 Not Found\r\nContent-Length: 29\r\n\r\n{\"reason\":\"unknown-request\"}  ");
    EXPECT_FALSE(postPageAnswer({late.port, "segredo"}, json, nullptr));  // expired or unknown request
}
