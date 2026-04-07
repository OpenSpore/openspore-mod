#include "SseClient.h"
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace OpenSpore {

static std::wstring ToWideStr(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

SseClient::SseClient() = default;

SseClient::~SseClient() {
    Disconnect();
}

void SseClient::SetEventCallback(SseEventCallback callback) {
    mCallback = std::move(callback);
}

void SseClient::Connect(const std::string& host, int port, const std::string& path) {
    Disconnect();
    mRunning = true;
    mThread = std::thread(&SseClient::WorkerThread, this, host, port, path);
}

void SseClient::Disconnect() {
    mRunning = false;
    if (mThread.joinable()) {
        mThread.join();
    }
}

void SseClient::Update() {
    std::vector<SseEvent> events;
    {
        std::lock_guard<std::mutex> lock(mEventMutex);
        events.swap(mPendingEvents);
    }
    for (auto& ev : events) {
        if (mCallback) {
            mCallback(ev);
        }
    }
}

void SseClient::WorkerThread(std::string host, int port, std::string path) {
    while (mRunning) {
        HINTERNET hSession = WinHttpOpen(L"OpenSporeMod-SSE/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            Sleep(5000);
            continue;
        }

        // Set longer timeouts for SSE
        DWORD timeout = 0; // infinite for receive
        WinHttpSetTimeouts(hSession, 10000, 10000, 0, 0);

        std::wstring wHost = ToWideStr(host);
        HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), (INTERNET_PORT)port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            Sleep(5000);
            continue;
        }

        std::wstring wPath = ToWideStr(path);
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            Sleep(5000);
            continue;
        }

        std::wstring headers = L"Accept: text/event-stream\r\nCache-Control: no-cache\r\n";
        BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!ok || !WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            Sleep(5000);
            continue;
        }

        // Read SSE stream
        mBuffer.clear();
        mCurrentEvent.clear();
        mCurrentData.clear();

        while (mRunning) {
            DWORD bytesAvailable = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) break;
            if (bytesAvailable == 0) {
                Sleep(100);
                continue;
            }

            std::vector<char> buf(bytesAvailable);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead)) break;

            mBuffer.append(buf.data(), bytesRead);
            ParseSseChunk(mBuffer);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (mRunning) {
            Sleep(3000); // Reconnect delay
        }
    }
}

void SseClient::ParseSseChunk(const std::string& chunk) {
    // Process complete lines from buffer
    size_t pos = 0;
    while (pos < mBuffer.size()) {
        size_t lineEnd = mBuffer.find('\n', pos);
        if (lineEnd == std::string::npos) break;

        std::string line = mBuffer.substr(pos, lineEnd - pos);
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        pos = lineEnd + 1;

        if (line.empty()) {
            // Empty line = end of event
            if (!mCurrentData.empty()) {
                SseEvent ev;
                ev.name = mCurrentEvent.empty() ? "message" : mCurrentEvent;
                ev.data = mCurrentData;

                std::lock_guard<std::mutex> lock(mEventMutex);
                mPendingEvents.push_back(std::move(ev));
            }
            mCurrentEvent.clear();
            mCurrentData.clear();
        } else if (line[0] == ':') {
            // Comment line (ping keepalive), ignore
        } else if (line.substr(0, 6) == "event:") {
            mCurrentEvent = line.substr(6);
            // Trim leading space
            if (!mCurrentEvent.empty() && mCurrentEvent[0] == ' ') {
                mCurrentEvent = mCurrentEvent.substr(1);
            }
        } else if (line.substr(0, 5) == "data:") {
            std::string data = line.substr(5);
            if (!data.empty() && data[0] == ' ') {
                data = data.substr(1);
            }
            if (!mCurrentData.empty()) {
                mCurrentData += "\n";
            }
            mCurrentData += data;
        }
    }

    // Keep unprocessed data in buffer
    mBuffer = mBuffer.substr(pos);
}

} // namespace OpenSpore
