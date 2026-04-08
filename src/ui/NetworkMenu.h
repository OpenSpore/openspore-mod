#pragma once

#include <Spore\UTFWin\IWindow.h>
#include <Spore\UTFWin\Window.h>
#include <Spore\UTFWin\IButton.h>
#include <Spore\UTFWin\ITextEdit.h>
#include <Spore\UTFWin\IWinProc.h>
#include <Spore\UTFWin\IWindowManager.h>
#include <Spore\UTFWin\Message.h>

#include "../net/ApiClient.h"

#include <string>
#include <functional>
#include <unordered_map>

namespace OpenSpore {

// Button command IDs
static constexpr uint32_t kBtnConnect = 0x4F534E01;
static constexpr uint32_t kBtnClose   = 0x4F534E02;
static constexpr uint32_t kBtnResetId = 0x4F534E03;

// Control IDs
static constexpr uint32_t kCtrlStatus      = 0x4F534E10;
static constexpr uint32_t kCtrlHostEdit    = 0x4F534E11;
static constexpr uint32_t kCtrlPortEdit    = 0x4F534E12;
static constexpr uint32_t kCtrlPlayersList = 0x4F534E13;

struct ConnectParams {
    std::string host;
    int port;
};

/// Modal network menu toggled with F9.
class NetworkMenu : public UTFWin::DefaultWinProc<UTFWin::kEventFlagBasicInput | UTFWin::kEventFlagAdvanced> {
public:
    using OnConnectCallback = std::function<void(const ConnectParams&)>;
    using OnResetCallback   = std::function<void()>;

    NetworkMenu();
    ~NetworkMenu();

    void SetCallbacks(OnConnectCallback onConnect, OnResetCallback onReset);

    void Show();
    void Hide();
    bool IsVisible() const { return mVisible; }

    void SetStatus(const std::string& status, bool connected);
    void SetPlayers(const std::unordered_map<std::string, EmpireData>& players);
    void SetDefaultAddress(const std::string& host, int port);

private:
    bool HandleUIMessage(UTFWin::IWindow* pWindow, const UTFWin::Message& message) override;

    void Build();

    static WindowPtr MakePanel(float x1, float y1, float x2, float y2,
                                Math::Color fill, Math::Color shade = {0,0,0,0});
    static WindowPtr MakeLabel(float x1, float y1, float x2, float y2,
                                const char16_t* text, Math::Color textColor = {255,255,255,255});
    static eastl::intrusive_ptr<UTFWin::IButton>   MakeButton(float x1, float y1, float x2, float y2,
                                                               const char16_t* text, uint32_t commandID);
    static eastl::intrusive_ptr<UTFWin::ITextEdit> MakeTextEdit(float x1, float y1, float x2, float y2,
                                                                 const char16_t* placeholder, uint32_t controlID);

    static constexpr float kW  = 500.0f;
    static constexpr float kH  = 420.0f;
    static constexpr float kX  = (1024.0f - kW) * 0.5f;
    static constexpr float kY  = (768.0f  - kH) * 0.5f;

    WindowPtr                              mpRoot;
    eastl::intrusive_ptr<UTFWin::ITextEdit> mpHostEdit;
    eastl::intrusive_ptr<UTFWin::ITextEdit> mpPortEdit;
    WindowPtr                              mpPlayersList;

    bool mVisible = false;
    bool mBuilt   = false;

    OnConnectCallback mOnConnect;
    OnResetCallback   mOnReset;
};

} // namespace OpenSpore
