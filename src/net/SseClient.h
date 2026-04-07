#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace OpenSpore {

struct SseEvent {
    std::string name;  // event type (e.g. "empire:joined", "diplomacy:offer")
    std::string data;  // JSON payload
};

using SseEventCallback = std::function<void(const SseEvent&)>;

/// Server-Sent Events client that maintains a persistent HTTP connection
/// to /events/stream and dispatches events on the game thread via Update().
class SseClient {
public:
    SseClient();
    ~SseClient();

    void Connect(const std::string& host, int port, const std::string& path);
    void SetEventCallback(SseEventCallback callback);

    /// Dispatch queued events on the game thread.
    void Update();

    void Disconnect();

private:
    void WorkerThread(std::string host, int port, std::string path);
    void ParseSseChunk(const std::string& chunk);

    std::atomic<bool> mRunning{false};
    std::thread mThread;

    SseEventCallback mCallback;

    std::mutex mEventMutex;
    std::vector<SseEvent> mPendingEvents;

    // SSE parser state
    std::string mCurrentEvent;
    std::string mCurrentData;
    std::string mBuffer;
};

} // namespace OpenSpore
