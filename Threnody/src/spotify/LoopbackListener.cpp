#include "spotify/LoopbackListener.h"

#include "util/Log.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <chrono>
#include <string_view>

namespace threnody::spotify {
namespace {

struct WinsockSession {
    WinsockSession() { ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
    ~WinsockSession() {
        if (ok) {
            WSACleanup();
        }
    }
    WSADATA data{};
    bool ok{false};
};

struct SocketCloser {
    void operator()(SOCKET s) const noexcept { closesocket(s); }
};
class UniqueSocket {
public:
    explicit UniqueSocket(SOCKET s = INVALID_SOCKET) : m_socket(s) {}
    ~UniqueSocket() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
        }
    }
    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;
    [[nodiscard]] SOCKET get() const noexcept { return m_socket; }
    [[nodiscard]] bool valid() const noexcept { return m_socket != INVALID_SOCKET; }

private:
    SOCKET m_socket;
};

int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2])));
            i += 2;
        } else if (text[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parseQuery(std::string_view query) {
    std::map<std::string, std::string> result;
    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            result[percentDecode(pair.substr(0, eq))] = percentDecode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            result[percentDecode(pair)] = "";
        }
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
    }
    return result;
}

void respond(SOCKET client, std::string_view status, std::string_view body) {
    const std::string head = std::format(
        "HTTP/1.1 {}\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
        status, body.size());
    send(client, head.data(), static_cast<int>(head.size()), 0);
    send(client, body.data(), static_cast<int>(body.size()), 0);
}

constexpr std::string_view successPage =
    "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\"><title>Threnody</title>"
    "<body style=\"background:#0a0a0a;color:#ededed;font-family:'Segoe UI Variable',system-ui;display:grid;"
    "place-items:center;height:100vh;margin:0\"><div style=\"text-align:center\"><h1 style=\"font-weight:500\">"
    "Threnody está conectado a Spotify</h1><p style=\"color:#a1a1a1\">Ya puedes cerrar esta pestaña.</p></div></body></html>";

}  // namespace

LoopbackListener::LoopbackListener(unsigned short port, std::string path, unsigned timeoutSeconds, Handler onRedirect)
    : m_port(port), m_path(std::move(path)), m_timeoutSeconds(timeoutSeconds), m_onRedirect(std::move(onRedirect)) {
    m_thread = std::jthread{[this](std::stop_token stop) { run(stop); }};
}

LoopbackListener::~LoopbackListener() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void LoopbackListener::run(std::stop_token stop) {
    WinsockSession winsock;
    if (!winsock.ok) {
        m_onRedirect({.error = "WSAStartup failed"});
        return;
    }

    UniqueSocket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (!listener.valid()) {
        m_onRedirect({.error = std::format("socket() failed: {}", WSAGetLastError())});
        return;
    }
    sockaddr_in address{.sin_family = AF_INET, .sin_port = htons(m_port)};
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        m_onRedirect({.error = std::format("no se pudo escuchar en el puerto {} (error {})", m_port, WSAGetLastError())});
        return;
    }
    if (listen(listener.get(), 4) != 0) {
        m_onRedirect({.error = std::format("listen() failed: {}", WSAGetLastError())});
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(m_timeoutSeconds);
    while (!stop.stop_requested()) {
        if (std::chrono::steady_clock::now() > deadline) {
            m_onRedirect({.error = "la autorización caducó sin respuesta del navegador"});
            return;
        }
        fd_set readable{};
        FD_ZERO(&readable);
        FD_SET(listener.get(), &readable);
        timeval wait{.tv_sec = 0, .tv_usec = 250'000};
        const int ready = select(0, &readable, nullptr, nullptr, &wait);
        if (ready <= 0) {
            continue;
        }

        UniqueSocket client{accept(listener.get(), nullptr, nullptr)};
        if (!client.valid()) {
            continue;
        }
        std::array<char, 8192> buffer{};
        const int received = recv(client.get(), buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
        if (received <= 0) {
            continue;
        }
        const std::string_view request{buffer.data(), static_cast<std::size_t>(received)};
        const std::size_t lineEnd = request.find("\r\n");
        const std::string_view line = request.substr(0, lineEnd);
        // "GET /callback?code=...&state=... HTTP/1.1"
        const std::size_t firstSpace = line.find(' ');
        const std::size_t secondSpace = line.find(' ', firstSpace + 1);
        if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos ||
            line.substr(0, firstSpace) != "GET") {
            respond(client.get(), "400 Bad Request", "Bad request");
            continue;
        }
        const std::string_view target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        const std::size_t question = target.find('?');
        const std::string_view path = target.substr(0, question);
        if (path != m_path) {
            respond(client.get(), "404 Not Found", "Not found");
            continue;
        }
        Redirect redirect{.query = parseQuery(question == std::string_view::npos ? std::string_view{}
                                                                                  : target.substr(question + 1))};
        respond(client.get(), "200 OK", successPage);
        m_onRedirect(std::move(redirect));
        return;
    }
}

}  // namespace threnody::spotify
