#include "HttpClient.h"
#include <thread>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace OpenSpore {

HttpClient::HttpClient() {
    mSession = WinHttpOpen(
        L"OpenSporeMod/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
}

HttpClient::~HttpClient() {
    Shutdown();
    if (mSession) {
        WinHttpCloseHandle(mSession);
        mSession = nullptr;
    }
}

void HttpClient::SetBaseUrl(const std::string& host, int port) {
    mHost = host;
    mPort = port;
}

void HttpClient::SetBearerToken(const std::string& token) {
    mBearerToken = token;
}

void HttpClient::Shutdown() {
    mShutdown = true;
}

void HttpClient::Get(const std::string& path, HttpCallback callback) {
    if (mShutdown) return;
    std::thread([this, path, callback]() {
        auto response = DoRequest("GET", path, "");
        std::lock_guard<std::mutex> lock(mCallbackMutex);
        mPendingCallbacks.push_back({callback, response});
    }).detach();
}

void HttpClient::Post(const std::string& path, const std::string& jsonBody, HttpCallback callback) {
    if (mShutdown) return;
    std::thread([this, path, jsonBody, callback]() {
        auto response = DoRequest("POST", path, jsonBody);
        std::lock_guard<std::mutex> lock(mCallbackMutex);
        mPendingCallbacks.push_back({callback, response});
    }).detach();
}

void HttpClient::Patch(const std::string& path, const std::string& jsonBody, HttpCallback callback) {
    if (mShutdown) return;
    std::thread([this, path, jsonBody, callback]() {
        auto response = DoRequest("PATCH", path, jsonBody);
        std::lock_guard<std::mutex> lock(mCallbackMutex);
        mPendingCallbacks.push_back({callback, response});
    }).detach();
}

void HttpClient::Update() {
    std::vector<PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(mCallbackMutex);
        callbacks.swap(mPendingCallbacks);
    }
    for (auto& pc : callbacks) {
        if (pc.callback) {
            pc.callback(pc.response);
        }
    }
}

static std::wstring ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

HttpResponse HttpClient::DoRequest(const std::string& method, const std::string& path,
                                   const std::string& body) {
    HttpResponse response;

    if (!mSession) {
        response.error = "WinHTTP session not initialized";
        return response;
    }

    std::wstring wHost = ToWide(mHost);
    HINTERNET hConnect = WinHttpConnect(mSession, wHost.c_str(), (INTERNET_PORT)mPort, 0);
    if (!hConnect) {
        response.error = "WinHttpConnect failed";
        return response;
    }

    std::wstring wMethod = ToWide(method);
    std::wstring wPath = ToWide(path);

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, wMethod.c_str(), wPath.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0  // No HTTPS for local dev; add WINHTTP_FLAG_SECURE for production
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        response.error = "WinHttpOpenRequest failed";
        return response;
    }

    // Set headers
    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!mBearerToken.empty()) {
        headers += L"Authorization: Bearer " + ToWide(mBearerToken) + L"\r\n";
    }

    BOOL result = WinHttpSendRequest(
        hRequest,
        headers.c_str(), (DWORD)headers.length(),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str(),
        (DWORD)body.size(),
        (DWORD)body.size(),
        0
    );

    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        response.error = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        return response;
    }

    result = WinHttpReceiveResponse(hRequest, nullptr);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        response.error = "WinHttpReceiveResponse failed";
        return response;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    response.statusCode = (int)statusCode;

    // Read body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
        responseBody.append(buffer.data(), bytesRead);
    }

    response.body = responseBody;
    response.success = (statusCode >= 200 && statusCode < 300);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return response;
}

} // namespace OpenSpore
