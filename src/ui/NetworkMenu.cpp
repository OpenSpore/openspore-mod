#include "NetworkMenu.h"
#include <Spore\UTFWin\IWindowManager.h>

using namespace UTFWin;

namespace OpenSpore {

static eastl::string16 ToStr16(const std::string& s) {
    eastl::string16 r;
    for (unsigned char c : s) r += (char16_t)c;
    return r;
}

// Helper: get IWindow* from an IWindowList_t iterator
static IWindow* WinFromIt(IWindowList_t::iterator it) {
    return static_cast<IWindow*>(&*it);
}

// ============================================================================
NetworkMenu::NetworkMenu() = default;

NetworkMenu::~NetworkMenu() {
    if (mBuilt && mpRoot) {
        WindowManager.GetMainWindow()->RemoveWindow(mpRoot.get());
    }
}

void NetworkMenu::SetCallbacks(OnConnectCallback onConnect, OnResetCallback onReset) {
    mOnConnect = std::move(onConnect);
    mOnReset   = std::move(onReset);
}

void NetworkMenu::SetDefaultAddress(const std::string& host, int port) {
    if (!mBuilt) return;
    if (mpHostEdit) mpHostEdit->SetText(ToStr16(host).c_str(), (int)host.size());
    if (mpPortEdit) {
        auto ps = std::to_string(port);
        mpPortEdit->SetText(ToStr16(ps).c_str(), (int)ps.size());
    }
}

void NetworkMenu::Show() {
    if (!mBuilt) Build();
    if (mpRoot)  mpRoot->SetVisible(true);
    mVisible = true;
}

void NetworkMenu::Hide() {
    if (mpRoot) mpRoot->SetVisible(false);
    mVisible = false;
}

void NetworkMenu::SetStatus(const std::string& status, bool connected) {
    if (!mBuilt || !mpRoot) return;
    IWindow* pLabel = mpRoot->FindWindowByID(kCtrlStatus, true);
    if (pLabel) {
        pLabel->SetCaption(ToStr16(status).c_str());
        pLabel->SetShadeColor(connected
            ? Math::Color(80, 220, 80, 255)
            : Math::Color(220, 80, 80, 255));
    }
}

void NetworkMenu::SetPlayers(const std::unordered_map<std::string, EmpireData>& players) {
    if (!mBuilt || !mpPlayersList) return;

    IWindow* list = mpPlayersList.get();
    // Remove all children
    while (list->GetChildrenBegin() != list->GetChildrenEnd()) {
        list->RemoveWindow(WinFromIt(list->GetChildrenBegin()));
    }

    float y = 0.0f;
    const float kRowH  = 24.0f;
    const float kListW = kW - 40.0f;

    if (players.empty()) {
        auto label = MakeLabel(0, y, kListW, y + kRowH,
            u"No other players online", {160, 160, 160, 255});
        list->AddWindow(label.get());
        return;
    }

    for (auto& [id, emp] : players) {
        auto swatch = MakePanel(4, y + 4, 18, y + 18,
            Math::Color(emp.color[0], emp.color[1], emp.color[2], 255));
        list->AddWindow(swatch.get());

        eastl::string16 caption = ToStr16(emp.name) + u"  \u2014  " + ToStr16(emp.homeWorld);
        if (!emp.online) caption += u" (offline)";

        auto nameLabel = MakeLabel(24, y, kListW - 8, y + kRowH, caption.c_str(),
            emp.online ? Math::Color(220, 220, 220, 255) : Math::Color(120, 120, 120, 255));
        list->AddWindow(nameLabel.get());

        y += kRowH + 2.0f;
    }
}

// ============================================================================
// Build
// ============================================================================

void NetworkMenu::Build() {
    mBuilt = true;

    // Full-screen backdrop
    mpRoot = MakePanel(0, 0, 1024, 768, Math::Color(0, 0, 0, 140));
    mpRoot->SetVisible(false);

    // Dialog panel
    auto dialog = MakePanel(kX, kY, kX + kW, kY + kH, Math::Color(18, 20, 30, 245));

    // Title bar
    auto titleBar = MakePanel(0, 0, kW, 40, Math::Color(30, 40, 70, 255));
    auto titleLabel = MakeLabel(10, 6, kW - 44, 34, u"OpenSpore  Multiplayer",
        Math::Color(200, 220, 255, 255));
    titleBar->AddWindow(titleLabel.get());
    auto closeBtn = MakeButton(kW - 36, 4, kW - 4, 36, u"X", kBtnClose);
    titleBar->AddWindow(closeBtn->ToWindow());
    dialog->AddWindow(titleBar.get());

    // Server address
    auto connLabel = MakeLabel(14, 52, 200, 70, u"Server address:",
        Math::Color(160, 180, 220, 255));
    dialog->AddWindow(connLabel.get());

    mpHostEdit = MakeTextEdit(14, 72, 340, 96, u"localhost", kCtrlHostEdit);
    dialog->AddWindow(mpHostEdit->ToWindow());

    auto colon = MakeLabel(344, 72, 356, 96, u":", {180, 180, 180, 255});
    dialog->AddWindow(colon.get());

    mpPortEdit = MakeTextEdit(358, 72, 430, 96, u"8080", kCtrlPortEdit);
    mpPortEdit->SetMaxTextLength(5);
    dialog->AddWindow(mpPortEdit->ToWindow());

    auto connectBtn = MakeButton(438, 72, kW - 14, 96, u"Connect", kBtnConnect);
    dialog->AddWindow(connectBtn->ToWindow());

    // Status row
    auto statusBg = MakePanel(14, 104, kW - 14, 128, Math::Color(10, 10, 18, 200));
    auto statusLabel = MakeLabel(8, 4, kW - 42, 20, u"Not connected",
        Math::Color(220, 80, 80, 255));
    statusLabel->SetControlID(kCtrlStatus);
    statusBg->AddWindow(statusLabel.get());
    dialog->AddWindow(statusBg.get());

    // Players header
    auto playersHeader = MakeLabel(14, 136, kW - 14, 154, u"Online players:",
        Math::Color(160, 180, 220, 255));
    dialog->AddWindow(playersHeader.get());

    // Players list area
    float listTop = 156.0f;
    float listH   = kH - listTop - 60.0f;
    mpPlayersList = MakePanel(14, listTop, kW - 14, listTop + listH,
        Math::Color(10, 10, 18, 180));
    mpPlayersList->SetControlID(kCtrlPlayersList);
    dialog->AddWindow(mpPlayersList.get());

    // Bottom buttons
    float btnY = kH - 50.0f;
    auto resetBtn  = MakeButton(14, btnY, 200, btnY + 34, u"Reset my identity", kBtnResetId);
    auto closeBtn2 = MakeButton(kW - 120, btnY, kW - 14, btnY + 34, u"Close", kBtnClose);
    dialog->AddWindow(resetBtn->ToWindow());
    dialog->AddWindow(closeBtn2->ToWindow());

    dialog->AddWinProc(static_cast<UTFWin::IWinProc*>(this));
    mpRoot->AddWindow(dialog.get());
    WindowManager.GetMainWindow()->AddWindow(mpRoot.get());
}

// ============================================================================
// Event handling
// ============================================================================

bool NetworkMenu::HandleUIMessage(IWindow* pWindow, const Message& message) {
    if (!message.IsType(kMsgButtonClick)) return false;

    uint32_t cmd = message.ButtonClick.commandID;

    if (cmd == kBtnClose) {
        Hide();
        return true;
    }

    if (cmd == kBtnConnect) {
        ConnectParams p;
        if (mpHostEdit) {
            const char16_t* h = mpHostEdit->GetText();
            while (h && *h) p.host += (char)(*h++);
        }
        if (p.host.empty()) p.host = "localhost";

        std::string portStr;
        if (mpPortEdit) {
            const char16_t* ps = mpPortEdit->GetText();
            while (ps && *ps) portStr += (char)(*ps++);
        }
        p.port = portStr.empty() ? 8080 : std::stoi(portStr);

        if (mOnConnect) mOnConnect(p);
        return true;
    }

    if (cmd == kBtnResetId) {
        if (mOnReset) mOnReset();
        return true;
    }

    return false;
}

// ============================================================================
// Factory helpers
// ============================================================================

WindowPtr NetworkMenu::MakePanel(float x1, float y1, float x2, float y2,
                                  Math::Color fill, Math::Color shade) {
    WindowPtr w = new Window();
    w->SetArea(Math::Rectangle(x1, y1, x2, y2));
    w->SetFillColor(fill);
    if (shade.a > 0) w->SetShadeColor(shade);
    return w;
}

WindowPtr NetworkMenu::MakeLabel(float x1, float y1, float x2, float y2,
                                  const char16_t* text, Math::Color textColor) {
    WindowPtr w = new Window();
    w->SetArea(Math::Rectangle(x1, y1, x2, y2));
    w->SetFillColor(Math::Color(0, 0, 0, 0));
    w->SetCaption(text);
    w->SetShadeColor(textColor);
    return w;
}

eastl::intrusive_ptr<IButton> NetworkMenu::MakeButton(float x1, float y1, float x2, float y2,
                                                       const char16_t* text, uint32_t commandID) {
    eastl::intrusive_ptr<IButton> btn = IButton::Create();
    btn->ToWindow()->SetArea(Math::Rectangle(x1, y1, x2, y2));
    btn->ToWindow()->SetCaption(text);
    btn->ToWindow()->SetCommandID(commandID);
    btn->ToWindow()->SetFillColor(Math::Color(50, 65, 100, 230));
    btn->ToWindow()->SetShadeColor(Math::Color(200, 210, 255, 255));
    return btn;
}

eastl::intrusive_ptr<ITextEdit> NetworkMenu::MakeTextEdit(float x1, float y1, float x2, float y2,
                                                           const char16_t* placeholder, uint32_t controlID) {
    eastl::intrusive_ptr<ITextEdit> edit = ITextEdit::Create();
    edit->ToWindow()->SetArea(Math::Rectangle(x1, y1, x2, y2));
    edit->ToWindow()->SetControlID(controlID);
    edit->ToWindow()->SetFillColor(Math::Color(25, 28, 42, 240));
    edit->ToWindow()->SetShadeColor(Math::Color(180, 190, 220, 255));
    edit->SetText(placeholder, -1);
    return edit;
}

} // namespace OpenSpore
