#include "NetworkMenu.h"
#include <Spore\UTFWin\IWindowManager.h>

namespace OpenSpore {

// Helper: convert std::string (ASCII/UTF-8) to eastl::string16
static eastl::string16 ToStr16(const std::string& s) {
    eastl::string16 result;
    for (unsigned char c : s) result += (char16_t)c;
    return result;
}

// ============================================================================
// NetworkMenu
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
    if (mpPortEdit) mpPortEdit->SetText(ToStr16(std::to_string(port)).c_str(), 5);
}

void NetworkMenu::Show() {
    if (!mBuilt) Build();
    if (mpRoot) mpRoot->SetVisible(true);
    mVisible = true;
}

void NetworkMenu::Hide() {
    if (mpRoot) mpRoot->SetVisible(false);
    mVisible = false;
}

void NetworkMenu::SetStatus(const std::string& status, bool connected) {
    if (!mBuilt || !mpRoot) return;

    // Find status label child window
    IWindow* pLabel = mpRoot->FindWindowByID(kCtrlStatus);
    if (pLabel) {
        auto wstr = ToStr16(status);
        pLabel->SetCaption(wstr.c_str());
        // Green if connected, red if not
        pLabel->SetShadeColor(connected
            ? Math::Color(80, 220, 80, 255)
            : Math::Color(220, 80, 80, 255));
    }
}

void NetworkMenu::SetPlayers(const std::unordered_map<std::string, EmpireData>& players) {
    if (!mBuilt || !mpPlayersList) return;

    // Remove existing player rows (children after the header)
    auto* list = mpPlayersList.get();
    // Remove all children first
    while (list->GetChildrenBegin() != list->GetChildrenEnd()) {
        list->RemoveWindow(*list->GetChildrenBegin());
    }

    float rowY = 0.0f;
    const float kRowH = 24.0f;
    const float kListW = kW - 40.0f; // matches the list panel width

    if (players.empty()) {
        auto label = MakeLabel(0, rowY, kListW, rowY + kRowH,
            u"No other players online", {160, 160, 160, 255});
        list->AddWindow(label.get());
        return;
    }

    for (auto& [id, emp] : players) {
        float y = rowY;

        // Color swatch (small square)
        auto swatch = MakePanel(4.0f, y + 4.0f, 18.0f, y + 18.0f,
            Math::Color(emp.color[0], emp.color[1], emp.color[2], 255));
        list->AddWindow(swatch.get());

        // Player name + home world
        eastl::string16 caption = ToStr16(emp.name) + u" \u2014 " + ToStr16(emp.homeWorld);
        if (!emp.online) caption += u" (offline)";

        auto nameLabel = MakeLabel(24.0f, y, kListW - 8.0f, y + kRowH,
            caption.c_str(),
            emp.online ? Math::Color(220, 220, 220, 255) : Math::Color(120, 120, 120, 255));
        list->AddWindow(nameLabel.get());

        rowY += kRowH + 2.0f;
    }
}

// ============================================================================
// Build (one-time window construction)
// ============================================================================

void NetworkMenu::Build() {
    mBuilt = true;

    // Backdrop (semi-transparent dark overlay covering the whole screen)
    mpRoot = MakePanel(0, 0, 1024, 768, Math::Color(0, 0, 0, 140));
    mpRoot->SetVisible(false);

    // Dialog panel
    auto dialog = MakePanel(kX, kY, kX + kW, kY + kH,
        Math::Color(18, 20, 30, 245), Math::Color(60, 80, 120, 80));

    // Title bar
    auto titleBar = MakePanel(0, 0, kW, 40, Math::Color(30, 40, 70, 255));
    auto titleLabel = MakeLabel(10, 6, kW - 40, 34, u"OpenSpore \u2014 Multiplayer",
        Math::Color(200, 220, 255, 255));
    titleBar->AddWindow(titleLabel.get());

    // Close button [X]
    auto closeBtn = MakeButton(kW - 36, 4, kW - 4, 36, u"X", kBtnClose);
    titleBar->AddWindow(closeBtn->ToWindow());
    dialog->AddWindow(titleBar.get());

    // --- Connection section ---
    auto connLabel = MakeLabel(14, 52, 200, 70, u"Server address:",
        Math::Color(160, 180, 220, 255));
    dialog->AddWindow(connLabel.get());

    // Host text edit
    mpHostEdit = MakeTextEdit(14, 72, 340, 96, u"localhost", kCtrlHostEdit);
    dialog->AddWindow(mpHostEdit->ToWindow());

    // ":" separator label
    auto colonLabel = MakeLabel(344, 72, 356, 96, u":",
        Math::Color(180, 180, 180, 255));
    dialog->AddWindow(colonLabel.get());

    // Port text edit
    mpPortEdit = MakeTextEdit(358, 72, 430, 96, u"8080", kCtrlPortEdit);
    mpPortEdit->SetMaxTextLength(5);
    dialog->AddWindow(mpPortEdit->ToWindow());

    // Connect button
    auto connectBtn = MakeButton(438, 72, kW - 14, 96, u"Connect", kBtnConnect);
    dialog->AddWindow(connectBtn->ToWindow());

    // Status bar
    auto statusBg = MakePanel(14, 104, kW - 14, 128, Math::Color(10, 10, 18, 200));
    auto statusLabel = MakeLabel(8, 4, kW - 28 - 14, 20, u"Not connected",
        Math::Color(220, 80, 80, 255));
    statusLabel->SetControlID(kCtrlStatus);
    statusBg->AddWindow(statusLabel.get());
    dialog->AddWindow(statusBg.get());
    mpStatusPanel = statusBg;

    // --- Online players section ---
    auto playersHeader = MakeLabel(14, 136, kW - 14, 154, u"Online players:",
        Math::Color(160, 180, 220, 255));
    dialog->AddWindow(playersHeader.get());

    // Players scroll area (fixed-height panel, no actual scrolling in MVP)
    float listTop  = 156.0f;
    float listH    = kH - listTop - 60.0f; // leave room for bottom buttons
    auto listBg = MakePanel(14, listTop, kW - 14, listTop + listH,
        Math::Color(10, 10, 18, 180));
    listBg->SetControlID(kCtrlPlayersList);
    mpPlayersList = listBg;
    dialog->AddWindow(listBg.get());

    // --- Bottom buttons ---
    float btnY = kH - 50.0f;
    auto resetBtn = MakeButton(14, btnY, 180, btnY + 34, u"Reset my identity", kBtnResetId);
    dialog->AddWindow(resetBtn->ToWindow());

    auto closeBtn2 = MakeButton(kW - 120, btnY, kW - 14, btnY + 34, u"Close", kBtnClose);
    dialog->AddWindow(closeBtn2->ToWindow());

    // Attach this WinProc to the dialog (catches button clicks)
    dialog->AddWinProc(this);

    mpRoot->AddWindow(dialog.get());

    // Add root to the game's main window
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

        // Read host
        if (mpHostEdit) {
            const char16_t* h = mpHostEdit->GetText();
            if (h) {
                while (*h) p.host += (char)(*h++);
            }
        }
        if (p.host.empty()) p.host = "localhost";

        // Read port
        std::string portStr;
        if (mpPortEdit) {
            const char16_t* pStr = mpPortEdit->GetText();
            if (pStr) {
                while (*pStr) portStr += (char)(*pStr++);
            }
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
// Static factory helpers
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
    w->SetFillColor(Math::Color(0, 0, 0, 0)); // transparent
    w->SetCaption(text);
    w->SetShadeColor(textColor);
    return w;
}

intrusive_ptr<IButton> NetworkMenu::MakeButton(float x1, float y1, float x2, float y2,
                                                const char16_t* text, uint32_t commandID) {
    intrusive_ptr<IButton> btn = IButton::Create();
    btn->ToWindow()->SetArea(Math::Rectangle(x1, y1, x2, y2));
    btn->ToWindow()->SetCaption(text);
    btn->ToWindow()->SetCommandID(commandID);
    btn->ToWindow()->SetFillColor(Math::Color(50, 65, 100, 230));
    btn->ToWindow()->SetShadeColor(Math::Color(200, 210, 255, 255));
    return btn;
}

intrusive_ptr<ITextEdit> NetworkMenu::MakeTextEdit(float x1, float y1, float x2, float y2,
                                                    const char16_t* placeholder, uint32_t controlID) {
    intrusive_ptr<ITextEdit> edit = ITextEdit::Create();
    edit->ToWindow()->SetArea(Math::Rectangle(x1, y1, x2, y2));
    edit->ToWindow()->SetControlID(controlID);
    edit->ToWindow()->SetFillColor(Math::Color(25, 28, 42, 240));
    edit->ToWindow()->SetShadeColor(Math::Color(180, 190, 220, 255));
    // Set placeholder as initial text
    edit->SetText(placeholder, -1);
    return edit;
}

} // namespace OpenSpore
