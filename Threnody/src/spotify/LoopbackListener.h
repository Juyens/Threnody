#pragma once

#include <functional>
#include <map>
#include <string>
#include <thread>

namespace threnody::spotify {

// One-shot HTTP listener on 127.0.0.1 that receives the OAuth redirect. It
// answers the browser with a small page, hands the query parameters to the
// handler (from its own thread) and exits. Anything else that connects gets a
// 404 and is ignored.
class LoopbackListener {
public:
    struct Redirect {
        std::map<std::string, std::string> query;  // Percent-decoded.
        std::string error;                          // Set when listening failed or timed out.
    };
    using Handler = std::function<void(Redirect)>;

    LoopbackListener(unsigned short port, std::string path, unsigned timeoutSeconds, Handler onRedirect);
    ~LoopbackListener();

    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;

private:
    void run(std::stop_token stop);

    unsigned short m_port{};
    std::string m_path;
    unsigned m_timeoutSeconds{};
    Handler m_onRedirect;
    std::jthread m_thread;
};

}  // namespace threnody::spotify
