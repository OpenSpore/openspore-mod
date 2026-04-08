#include "GalaxyOverlay.h"
#include <Spore\Simulator\SubSystem\StarManager.h>

using namespace UTFWin;

namespace OpenSpore {

// ============================================================================
// Helpers
// ============================================================================

static eastl::string16 ToStr16(const std::string& s) {
    eastl::string16 r;
    for (unsigned char c : s) r += (char16_t)c;
    return r;
}

static IWindow* WinFromIt(IWindowList_t::iterator it) {
    return static_cast<IWindow*>(&*it);
}

static WindowPtr MakePanel(float x1, float y1, float x2, float y2, Math::Color fill) {
    WindowPtr w = new Window();
    w->SetArea(Math::Rectangle(x1, y1, x2, y2));
    w->SetFillColor(fill);
    return w;
}

static WindowPtr MakeLabel(float x1, float y1, float x2, float y2,
                            const char16_t* text, Math::Color color) {
    WindowPtr w = new Window();
    w->SetArea(Math::Rectangle(x1, y1, x2, y2));
    w->SetFillColor(Math::Color(0, 0, 0, 0));
    w->SetCaption(text);
    w->SetShadeColor(color);
    return w;
}

// ============================================================================
// GalaxyOverlay
// ============================================================================

GalaxyOverlay::GalaxyOverlay() = default;

GalaxyOverlay::~GalaxyOverlay() {
    if (mBuilt && mpRoot) {
        WindowManager.GetMainWindow()->RemoveWindow(mpRoot.get());
    }
}

void GalaxyOverlay::Build() {
    mBuilt = true;

    float x1 = 1024.0f - kPanelW - kPadR;
    float y1 = kPadT;
    float x2 = 1024.0f - kPadR;
    float y2 = y1 + kHeaderH + kMaxRows * kRowH + 6.0f;

    mpRoot = MakePanel(x1, y1, x2, y2, Math::Color(12, 14, 22, 210));

    // Header bar
    auto header = MakePanel(0, 0, kPanelW, kHeaderH, Math::Color(25, 35, 65, 240));
    auto dot    = MakePanel(6, 8, 14, 16, Math::Color(80, 220, 80, 255));
    auto title  = MakeLabel(18, 4, kPanelW - 4, kHeaderH - 4,
                             u"OpenSpore Online", Math::Color(190, 210, 255, 255));
    header->AddWindow(dot.get());
    header->AddWindow(title.get());
    mpRoot->AddWindow(header.get());

    // Rows container
    mpRowsContainer = MakePanel(0, kHeaderH, kPanelW, kHeaderH + kMaxRows * kRowH + 4.0f,
                                 Math::Color(0, 0, 0, 0));
    mpRoot->AddWindow(mpRowsContainer.get());

    mpRoot->SetVisible(false);
    WindowManager.GetMainWindow()->AddWindow(mpRoot.get());
}

void GalaxyOverlay::Show() {
    if (!mBuilt) Build();
    if (mpRoot)  mpRoot->SetVisible(true);
    mVisible = true;
}

void GalaxyOverlay::Hide() {
    if (mpRoot) mpRoot->SetVisible(false);
    mVisible = false;
}

void GalaxyOverlay::Update(const std::unordered_map<std::string, EmpireData>& players,
                            bool selfConnected, const std::string& selfEmpireName) {
    if (!mBuilt) Build();
    RebuildPlayerRows(players, selfConnected, selfEmpireName);

    // Update header dot color
    if (mpRoot) {
        auto headerIt = mpRoot->GetChildrenBegin();
        if (headerIt != mpRoot->GetChildrenEnd()) {
            IWindow* header = WinFromIt(headerIt);
            auto dotIt = header->GetChildrenBegin();
            if (dotIt != header->GetChildrenEnd()) {
                WinFromIt(dotIt)->SetFillColor(selfConnected
                    ? Math::Color(80, 220, 80, 255)
                    : Math::Color(220, 80, 80, 255));
            }
        }
    }

    // Resize panel to match actual row count
    if (mpRoot) {
        int count = (int)players.size() + (selfConnected ? 1 : 0);
        if (count == 0) count = 1;
        float x1 = 1024.0f - kPanelW - kPadR;
        float y1 = kPadT;
        mpRoot->SetArea(Math::Rectangle(x1, y1,
                                        x1 + kPanelW,
                                        y1 + kHeaderH + (float)count * kRowH + 6.0f));
    }
}

void GalaxyOverlay::RebuildPlayerRows(const std::unordered_map<std::string, EmpireData>& players,
                                       bool selfConnected, const std::string& selfEmpireName) {
    if (!mpRowsContainer) return;

    // Clear children
    while (mpRowsContainer->GetChildrenBegin() != mpRowsContainer->GetChildrenEnd()) {
        mpRowsContainer->RemoveWindow(WinFromIt(mpRowsContainer->GetChildrenBegin()));
    }

    float y = 2.0f;

    // Self row
    if (selfConnected && !selfEmpireName.empty()) {
        auto dot = MakePanel(4, y + 5, 14, y + 15, Math::Color(80, 220, 80, 255));
        mpRowsContainer->AddWindow(dot.get());
        eastl::string16 caption = ToStr16(selfEmpireName) + u" (you)";
        auto label = MakeLabel(18, y + 1, kPanelW - 6, y + kRowH - 1,
                                caption.c_str(), Math::Color(200, 255, 200, 255));
        mpRowsContainer->AddWindow(label.get());
        y += kRowH;
    }

    if (players.empty()) {
        const char16_t* msg = selfConnected ? u"No other players" : u"Not connected  [F9]";
        auto label = MakeLabel(4, y, kPanelW - 6, y + kRowH, msg, Math::Color(140, 140, 140, 255));
        mpRowsContainer->AddWindow(label.get());
        return;
    }

    int shown = 0;
    for (auto& [id, emp] : players) {
        if (shown >= (int)kMaxRows - 1) {
            eastl::string16 more = u"+" + ToStr16(std::to_string((int)players.size() - shown)) + u" more";
            auto label = MakeLabel(4, y, kPanelW - 6, y + kRowH, more.c_str(),
                                    Math::Color(140, 140, 140, 255));
            mpRowsContainer->AddWindow(label.get());
            break;
        }

        auto swatch = MakePanel(4, y + 5, 14, y + 15,
            Math::Color(emp.color[0], emp.color[1], emp.color[2], 255));
        mpRowsContainer->AddWindow(swatch.get());

        eastl::string16 caption = ToStr16(emp.name);
        if (!emp.online) caption += u" (off)";

        Math::Color nameColor = emp.online
            ? Math::Color(220, 220, 220, 255)
            : Math::Color(100, 100, 100, 200);
        auto label = MakeLabel(18, y + 1, kPanelW - 6, y + kRowH - 1, caption.c_str(), nameColor);
        mpRowsContainer->AddWindow(label.get());

        MarkHomeStar(emp);

        y += kRowH;
        ++shown;
    }
}

// ============================================================================
// Star markers
// ============================================================================

void GalaxyOverlay::MarkHomeStar(const EmpireData& emp) {
    if (emp.homeWorld.empty()) return;
    auto* star = FindStarByName(emp.homeWorld);
    if (!star) return;

    // Only mark stars that are currently unowned (empireID == 0 or 0xFFFFFFFF sentinel)
    if (star->mEmpireID == 0 || (star->mEmpireID & 0xFF000000) == 0xFF000000) {
        star->mEmpireID = 0xFF000000
            | ((uint32_t)emp.color[0] << 16)
            | ((uint32_t)emp.color[1] << 8)
            |  (uint32_t)emp.color[2];
    }
}

void GalaxyOverlay::UnmarkAllStars(const std::unordered_map<std::string, EmpireData>& /*players*/) {
    auto& mgr = StarManager;
    // Iterate all stars in the star record grid
    for (auto& row : mgr.mStarRecordGrid) {
        for (auto& starPtr : row) {
            if (starPtr && (starPtr->mEmpireID & 0xFF000000) == 0xFF000000) {
                starPtr->mEmpireID = 0;
            }
        }
    }
}

Simulator::cStarRecord* GalaxyOverlay::FindStarByName(const std::string& name) {
    auto& mgr = StarManager;
    for (auto& row : mgr.mStarRecordGrid) {
        for (auto& starPtr : row) {
            if (!starPtr) continue;
            auto& n = starPtr->mName;
            if (n.size() != name.size()) continue;
            bool match = true;
            for (size_t i = 0; i < name.size(); ++i) {
                if ((char)n[i] != name[i]) { match = false; break; }
            }
            if (match) return starPtr.get();
        }
    }
    return nullptr;
}

} // namespace OpenSpore
