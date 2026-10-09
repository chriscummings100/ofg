// Native WinHTTP callbacks and browser Fetch streams provide bounded asynchronous transport with cancellation.
#include "platform/http-client.h"
#include "core/engine-error.h"
#include <atomic>
#include <mutex>
#include <cstring>
#include <array>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace ofg {
struct HttpRequest::State
{
    std::mutex mutex;
    std::optional<HttpResponse> response;
    std::atomic<bool> completed{false};
    std::shared_ptr<terrain::StreamingWake> wake;
    std::string url, body;
    size_t limit = 0;
    bool bypass = false;
#ifdef __EMSCRIPTEN__
    uint32_t browserId = 0;
#else
    std::atomic<HINTERNET> request{nullptr};
    HINTERNET connection = nullptr;
    std::vector<std::byte> bytes;
    std::array<std::byte, 65536> buffer;
    uint32_t status = 0;
    // Connection handles are cheap session children; WinHTTP reuses actual connections in the shared session.
    ~State()
    {
        if (connection)
            WinHttpCloseHandle(connection);
    }
#endif

    // Publish exactly one terminal outcome and wake the job owner after releasing the response lock.
    void finish(HttpResponse value)
    {
        {
            std::lock_guard lock(mutex);
            if (completed.exchange(true))
                return;
            response = std::move(value);
        }
        if (wake)
            wake->signal();
    }
};

#ifdef __EMSCRIPTEN__
// Fetch runs on the browser main runtime so its event loop never depends on a sleeping mesh/coordinator thread.
// clang-format off
EM_JS(
    void,
    startBrowserHttp,
    (uint32_t id, uintptr_t context, const char* url, const char* body, size_t limit, bool bypass),
    {
        const jobs = Module.ofgHttpJobs || (Module.ofgHttpJobs = new Map());
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 30000);
        jobs.set(id, controller);
        const address = UTF8ToString(url), payload = UTF8ToString(body);
        (async() => {
            let pointer = 0;
            try
            {
                const response = await fetch(address, {
                    method : payload ? 'POST' : 'GET',
                    body : payload || undefined,
                    headers : payload ? {'Content-Type' : 'application/json'} : undefined,
                    cache : bypass ? 'no-store' : 'default',
                    signal : controller.signal
                });
                const advertised = Number(response.headers.get('Content-Length'));
                if (advertised > limit)
                    throw new Error('HTTP response exceeds byte limit');
                const reader = response.body.getReader(), chunks = [];
                let length = 0;
                while (true)
                {
                    const part = await reader.read();
                    if (part.done)
                        break;
                    length += part.value.length;
                    if (length > limit)
                    {
                        await reader.cancel();
                        throw new Error('HTTP response exceeds byte limit');
                    }
                    chunks.push(part.value);
                }
                pointer = _malloc(Math.max(1, length));
                if (!pointer)
                    throw new Error('HTTP response allocation failed');
                let offset = 0;
                for (const chunk of chunks)
                {
                    HEAPU8.set(chunk, pointer + offset);
                    offset += chunk.length;
                }
                _ofgHttpFinished(context, response.status, pointer, length, 0);
            } catch (error)
            {
                controller.abort();
                const message = stringToNewUTF8(String(error));
                _ofgHttpFinished(context, 0, 0, 0, message);
                _free(message);
            } finally
            {
                if (pointer)
                    _free(pointer);
                clearTimeout(timer);
                jobs.delete(id);
            }
        })();
    }
);
// clang-format on

// Aborting a missing/completed ID is harmless and cannot cancel a newer request.
EM_JS(void, cancelBrowserHttp, (uint32_t id), {
    const controller = Module.ofgHttpJobs && Module.ofgHttpJobs.get(id);
    if (controller)
        controller.abort();
});

// Consume the retained browser completion context once; no exception crosses into JavaScript.
extern "C" EMSCRIPTEN_KEEPALIVE void ofgHttpFinished(
    uintptr_t context,
    uint32_t status,
    const std::byte* bytes,
    size_t size,
    const char* error
)
{
    std::unique_ptr<std::shared_ptr<HttpRequest::State>> owner(
        reinterpret_cast<std::shared_ptr<HttpRequest::State>*>(context)
    );
    auto state = *owner;
    if (state->completed)
        return;
    try
    {
        HttpResponse response;
        response.status = status;
        if (error)
            response.error = error;
        else if (size)
            response.bytes.assign(bytes, bytes + size);
        state->finish(std::move(response));
    } catch (const std::exception& failure)
    {
        state->finish({0, {}, failure.what()});
    }
}

// Starts a proxied request and transfers context ownership to the JavaScript completion.
void startOnBrowser(void* pointer)
{
    auto* context = static_cast<std::shared_ptr<HttpRequest::State>*>(pointer);
    auto state = *context;
    if (state->completed)
    {
        delete context;
        return;
    }
    startBrowserHttp(
        state->browserId,
        uintptr_t(context),
        state->url.c_str(),
        state->body.c_str(),
        state->limit,
        state->bypass
    );
}

// Runs cancellation on the same browser runtime that owns the AbortController.
void cancelOnBrowser(void* pointer)
{
    std::unique_ptr<std::shared_ptr<HttpRequest::State>> context(
        static_cast<std::shared_ptr<HttpRequest::State>*>(pointer)
    );
    cancelBrowserHttp((*context)->browserId);
}
#else
namespace {
// Share a WinHTTP session to retain DNS/TCP/TLS pooling across terrain requests.
struct HttpSession
{
    HINTERNET handle = nullptr;
    // Configure finite timeouts and asynchronous callbacks without opening a connection yet.
    HttpSession()
    {
        handle =
            WinHttpOpen(L"OFG Terrain/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC);
        if (!handle)
            throw EngineError("WinHTTP session creation failed.");
        if (!WinHttpSetTimeouts(handle, 10000, 10000, 30000, 30000))
        {
            WinHttpCloseHandle(handle);
            throw EngineError("WinHTTP timeout configuration failed.");
        }
    }
    // Close session resources after request owners have completed at application shutdown.
    ~HttpSession() { WinHttpCloseHandle(handle); }
};

// Convert a checked UTF-8 request URL to the native WinHTTP representation.
std::wstring wide(std::string_view value)
{
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!count)
        throw EngineError("Invalid UTF-8 HTTP URL.");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), count);
    return result;
}

// Detach the request handle before closing; WinHTTP retains its callback context through HANDLE_CLOSING.
void closeNative(const std::shared_ptr<HttpRequest::State>& state)
{
    if (auto request = state->request.exchange(nullptr))
        WinHttpCloseHandle(request);
}

// Consume WinHTTP events without letting exceptions or mutable engine objects cross the callback boundary.
void CALLBACK nativeEvent(HINTERNET request, DWORD_PTR context, DWORD event, void* information, DWORD length)
{
    auto* owner = reinterpret_cast<std::shared_ptr<HttpRequest::State>*>(context);
    if (!owner)
        return;
    auto state = *owner;
    if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
    {
        delete owner;
        return;
    }
    if (state->completed)
        return;
    try
    {
        BOOL ok = TRUE;
        switch (event)
        {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            ok = WinHttpReceiveResponse(request, nullptr);
            break;
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        {
            DWORD size = sizeof(DWORD);
            if (!WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    nullptr,
                    &state->status,
                    &size,
                    nullptr
                ))
                throw EngineError("Cannot read HTTP status.");
            wchar_t contentLength[32]{};
            size = sizeof(contentLength);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, contentLength, &size, nullptr))
                if (_wcstoui64(contentLength, nullptr, 10) > state->limit)
                    throw EngineError("HTTP response exceeds byte limit.");
            ok = WinHttpReadData(request, state->buffer.data(), DWORD(state->buffer.size()), nullptr);
            break;
        }
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            if (!length)
            {
                state->finish({state->status, std::move(state->bytes)});
                closeNative(state);
                return;
            }
            if (length > state->limit - state->bytes.size())
                throw EngineError("HTTP response exceeds byte limit.");
            state->bytes.insert(state->bytes.end(), state->buffer.begin(), state->buffer.begin() + length);
            ok = WinHttpReadData(request, state->buffer.data(), DWORD(state->buffer.size()), nullptr);
            break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            throw EngineError(
                "WinHTTP transport failed: " + std::to_string(static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError)
            );
        default:
            return;
        }
        if (!ok && GetLastError() != ERROR_IO_PENDING)
            throw EngineError("WinHTTP asynchronous operation failed.");
    } catch (const std::exception& failure)
    {
        state->finish({0, {}, failure.what()});
        closeNative(state);
    }
}
} // namespace
#endif

HttpRequest::HttpRequest(std::shared_ptr<State> state)
    : m_state(std::move(state))
{
}
HttpRequest::~HttpRequest()
{
    cancel();
}

std::unique_ptr<HttpRequest> HttpRequest::start(
    std::string url,
    size_t byteLimit,
    bool bypassCache,
    std::string body,
    std::shared_ptr<terrain::StreamingWake> wake
)
{
    if (url.empty() || url.size() > 8192 || !byteLimit || byteLimit > (2 << 20) || body.size() > (256 << 10))
        throw EngineError("Invalid HTTP request limits.");
    auto state = std::make_shared<State>();
    state->url = std::move(url);
    state->body = std::move(body);
    state->limit = byteLimit;
    state->bypass = bypassCache;
    state->wake = std::move(wake);
    auto result = std::unique_ptr<HttpRequest>(new HttpRequest(state));
#ifdef __EMSCRIPTEN__
    static std::atomic<uint32_t> next{1};
    state->browserId = next++;
    auto context = new std::shared_ptr<State>(state);
    if (!emscripten_proxy_async(
            emscripten_proxy_get_system_queue(),
            emscripten_main_runtime_thread_id(),
            startOnBrowser,
            context
        ))
    {
        delete context;
        throw EngineError("Cannot proxy browser HTTP request.");
    }
#else
    static HttpSession session;
    const auto address = wide(state->url);
    URL_COMPONENTS parsed{};
    parsed.dwStructSize = sizeof(parsed);
    parsed.dwHostNameLength = parsed.dwUrlPathLength = parsed.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(address.c_str(), DWORD(address.size()), 0, &parsed) ||
        (parsed.nScheme != INTERNET_SCHEME_HTTP && parsed.nScheme != INTERNET_SCHEME_HTTPS))
        throw EngineError("Expected absolute HTTP/HTTPS terrain URL.");
    std::wstring host(parsed.lpszHostName, parsed.dwHostNameLength);
    std::wstring path(parsed.lpszUrlPath, parsed.dwUrlPathLength);
    if (parsed.dwExtraInfoLength)
        path.append(parsed.lpszExtraInfo, parsed.dwExtraInfoLength);
    state->connection = WinHttpConnect(session.handle, host.c_str(), parsed.nPort, 0);
    if (!state->connection)
        throw EngineError("WinHTTP connection creation failed.");
    auto request = WinHttpOpenRequest(
        state->connection,
        state->body.empty() ? L"GET" : L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        parsed.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0
    );
    if (!request)
        throw EngineError("WinHTTP request creation failed.");
    state->request = request;
    auto* context = new std::shared_ptr<State>(state);
    DWORD_PTR contextValue = reinterpret_cast<DWORD_PTR>(context);
    if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &contextValue, sizeof(contextValue)) ||
        WinHttpSetStatusCallback(
            request,
            nativeEvent,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
            0
        ) == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        delete context;
        closeNative(state);
        throw EngineError("WinHTTP callback configuration failed.");
    }
    // Explicit no-cache headers bypass platform HTTP caches without changing immutable content identity.
    std::wstring headers = state->body.empty() ? L"" : L"Content-Type: application/json\r\n";
    if (bypassCache)
        headers += L"Cache-Control: no-cache, no-store\r\nPragma: no-cache\r\n";
    if (!WinHttpSendRequest(
            request,
            headers.c_str(),
            DWORD(headers.size()),
            state->body.empty() ? nullptr : state->body.data(),
            DWORD(state->body.size()),
            DWORD(state->body.size()),
            contextValue
        ) &&
        GetLastError() != ERROR_IO_PENDING)
    {
        state->finish({0, {}, "WinHTTP could not send request."});
        closeNative(state);
    }
#endif
    return result;
}

std::optional<HttpResponse> HttpRequest::take()
{
    std::lock_guard lock(m_state->mutex);
    return std::exchange(m_state->response, std::nullopt);
}

void HttpRequest::cancel()
{
    if (m_state->completed)
        return;
    m_state->finish({0, {}, {}, true});
#ifdef __EMSCRIPTEN__
    auto context = new std::shared_ptr<State>(m_state);
    if (!emscripten_proxy_async(
            emscripten_proxy_get_system_queue(),
            emscripten_main_runtime_thread_id(),
            cancelOnBrowser,
            context
        ))
        delete context;
#else
    closeNative(m_state);
#endif
}
} // namespace ofg
