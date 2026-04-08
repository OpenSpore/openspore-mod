#pragma once

#include <Spore\UTFWin\IWindow.h>
#include <Spore\UTFWin\Window.h>
#include <Spore\UTFWin\IButton.h>
#include <Spore\UTFWin\ITextEdit.h>
#include <Spore\UTFWin\IWinProc.h>
#include <Spore\UTFWin\IWindowManager.h>
#include <Spore\UTFWin\Message.h>
#include <Spore\UTFWin\UTFWinObject.h>

#include "../net/ApiClient.h"

#include <string>
#include <functional>
#include <unordered_map>

namespace OpenSpore {

using namespace UTFWin;

// Button command IDs
static constexpr uint32_t kBtnConnect  = 0x4F534E01; // OSN1
static constexpr uint32_t kBtnClose    = 0x4F534E02; // OSN2
static constexpr uint32_t kBtnResetId  = 0x4F534E03; // OSN3

// Control IDs for FindWindowByID
static constexpr uint32_t kCtrlStatus      = 0x4F534E10;
static constexpr uint32_t kCtrlHostEdit    = 0x4F534E11;
static constexpr uint32_t kCtrlPortEdit    = 0x4F534E12;
static constexpr uint32_t kCtrlPlayersList = 0x4F534E13;

struct ConnectParams {
    std::string host;
    int port;
};

/// Fullscreen modal network menu. Toggled with F9 key.
/// Shows server connection settings and lists online players.
class NetworkMenu : public DefaultWinProc<kEventFlagBasicInput | kEventFlagAdvanced> {
public:
    using OnConnectCallback = std::function<void(const ConnectParams&)>;
    using OnResetCallback   = std::function<void()>;

    NetworkMenu();
    ~NetworkMenu();

    void SetCallbacks(OnConnectCallback onConnect, OnResetCallback onReset);

    void Show();
    void Hide();
    bool IsVisible() const { return mVisible; }

    /// Call every frame to update status text and player list.
    void SetStatus(const std::string& status, bool connected);
    void SetPlayers(const std::unordered_map<std::string, EmpireData>& players);

    /// Populate host/port fields from saved config.
    void SetDefaultAddress(const std::string& host, int port);

private:
    bool HandleUIMessage(IWindow* pWindow, const Message& message) override;

    void Build();

    /// Create a styled panel Window.
    static WindowPtr MakePanel(float x1, float y1, float x2, float y2,
                                Math::Color fill, Math::Color shade = {0,0,0,0});
    /// Create a text label Window.
    static WindowPtr MakeLabel(float x1, float y1, float x2, float y2,
                                const char16_t* text, Math::Color textColor = {255,255,255,255});
    /// Create a button.
    static intrusive_ptr<IButton> MakeButton(float x1, float y1, float x2, float y2,
                                              const char16_t* text, uint32_t commandID);
    /// Create a text edit field.
    static intrusive_ptr<ITextEdit> MakeTextEdit(float x1, float y1, float x2, float y2,
                                                  const char16_t* placeholder, uint32_t controlID);

    // Layout constants (designed for 1024x768 minimum)
    static constexpr float kW = 500.0f;
    static constexpr float kH = 420.0f;
    static constexpr float kX = (1024.0f - kW) * 0.5f; // 262
    static constexpr float kY = (768.0f  - kH) * 0.5f; // 174

    WindowPtr                mpRoot;
    WindowPtr                mpStatusPanel;
    intrusive_ptr<ITextEdit> mpHostEdit;
    intrusive_ptr<ITextEdit> mpPortEdit;
    WindowPtr                mpPlayersList;

    bool mVisible = false;
    bool mBuilt   = false;

    OnConnectCallback mOnConnect;
    OnResetCallback   mOnReset;
};

} // namespace OpenSpore
