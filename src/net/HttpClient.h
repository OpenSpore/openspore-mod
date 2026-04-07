#pragma once

#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <windows.h>
#include <winhttp.h>

namespace OpenSpore {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool success = false;
    std::string error;
};

using HttpCallback = std::function<void(const HttpResponse&)>;

/// Async HTTP client using WinHTTP. All requests run on background threads
/// to avoid blocking Spore's game loop.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    void SetBaseUrl(const std::string& host, int port);
    void SetBearerToken(const std::string& token);

    // Async HTTP methods - callbacks invoked on the calling thread via Update()
    void Get(const std::string& path, HttpCallback callback);
    void Post(const std::string& path, const std::string& jsonBody, HttpCallback callback);
    void Patch(const std::string& path, const std::string& jsonBody, HttpCallback callback);

    // Must be called from the game thread to dispatch pending callbacks
    void Update();

    void Shutdown();

private:
    struct PendingCallback {
        HttpCallback callback;
        HttpResponse response;
    };

    HttpResponse DoRequest(const std::string& method, const std::string& path,
                           const std::string& body);

    std::string mHost;
    int mPort = 8080;
    std::string mBearerToken;

    std::mutex mCallbackMutex;
    std::vector<PendingCallback> mPendingCallbacks;

    std::atomic<bool> mShutdown{false};
    HINTERNET mSession = nullptr;
};

} // namespace OpenSpore
