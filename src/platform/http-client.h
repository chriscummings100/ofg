// Small bounded asynchronous HTTP requests; host callbacks retain state and never touch scene or GPU objects.
#pragma once
#include "terrain/streaming-wake.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ofg {
struct HttpResponse
{
    uint32_t status = 0;
    std::vector<std::byte> bytes;
    std::string error;
    bool cancelled = false;
};

class HttpRequest
{
public:
    struct State;
    // Starts GET or JSON POST, bounding decoded response bytes; URL must be absolute natively.
    static std::unique_ptr<HttpRequest> start(
        std::string url,
        size_t byteLimit,
        bool bypassCache = false,
        std::string body = {},
        std::shared_ptr<terrain::StreamingWake> wake = {}
    );
    // Cancels outstanding I/O without waiting; callbacks retain their own completion state.
    ~HttpRequest();
    // Transfers the single terminal response when available; repeated reads return nullopt.
    std::optional<HttpResponse> take();
    // Requests cancellation, racing safely with response completion.
    void cancel();

private:
    // Adopts a fully owned callback context before starting platform I/O.
    explicit HttpRequest(std::shared_ptr<State> state);
    std::shared_ptr<State> m_state;
};
} // namespace ofg
