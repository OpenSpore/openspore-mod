#include "GalaxyOverlay.h"
#include <Spore\Simulator\SubSystem\StarManager.h>

namespace OpenSpore {

// ============================================================================
// Helpers
// ============================================================================

static eastl::string16 ToStr16(const std::string& s) {
    eastl::string16 result;
    for (unsigned char c : s) result += (char16_t)c;
    return result;
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
    // Height = header + up to kMaxRows rows + small bottom pad
    float y2 = y1 + kHeaderH + kMaxRows * kRowH + 6.0f;

    // Outer panel
    mpRoot = MakePanel(x1, y1, x2, y2, Math::Color(12, 14, 22, 210));
    mpRoot->SetShadeColor(Math::Color(60, 80, 130, 60));

    // Header
    auto header = MakePanel(0, 0, kPanelW, kHeaderH, Math::Color(25, 35, 65, 240));
    // Colored dot (green = connected indicator)
    auto dot = MakePanel(6, 8, 14, 16, Math::Color(80, 220, 80, 255));
    header->AddWindow(dot.get());
    auto title = MakeLabel(18, 4, kPanelW - 4, kHeaderH - 4,
        u"OpenSpore Online", Math::Color(190, 210, 255, 255));
    header->AddWindow(title.get());
    mpRoot->AddWindow(header.get());

    // Rows container (positioned right below the header)
    mpRowsContainer = MakePanel(0, kHeaderH, kPanelW, kHeaderH + kMaxRows * kRowH + 4.0f,
        Math::Color(0, 0, 0, 0));
    mpRoot->AddWindow(mpRowsContainer.get());

    mpRoot->SetVisible(false);
    WindowManager.GetMainWindow()->AddWindow(mpRoot.get());
}

void GalaxyOverlay::Show() {
    if (!mBuilt) Build();
    if (mpRoot) mpRoot->SetVisible(true);
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

    // Update header dot color: green = connected, yellow = no server, red = offline
    // (The dot is the first child of the header, which is the first child of mpRoot)
    // We can re-set its fill color via the header child
    if (mpRoot) {
        // Children iteration: header is first child
        auto it = mpRoot->GetChildrenBegin();
        if (it != mpRoot->GetChildrenEnd()) {
            IWindow* header = *it;
            auto dotIt = header->GetChildrenBegin();
            if (dotIt != header->GetChildrenEnd()) {
                Math::Color dotColor = selfConnected
                    ? Math::Color(80, 220, 80, 255)   // green
                    : Math::Color(220, 80, 80, 255);  // red
                (*dotIt)->SetFillColor(dotColor);
            }
        }
    }

    // Dynamically resize panel height based on actual player count
    if (mpRoot) {
        int count = (int)players.size() + (selfConnected ? 1 : 0);
        if (count == 0) count = 1; // at least show "no players" row
        float x1 = 1024.0f - kPanelW - kPadR;
        float y1 = kPadT;
        float x2 = 1024.0f - kPadR;
        float y2 = y1 + kHeaderH + (float)count * kRowH + 6.0f;
        mpRoot->SetArea(Math::Rectangle(x1, y1, x2, y2));
    }
}

void GalaxyOverlay::RebuildPlayerRows(const std::unordered_map<std::string, EmpireData>& players,
                                       bool selfConnected, const std::string& selfEmpireName) {
    if (!mpRowsContainer) return;

    // Clear existing rows
    while (mpRowsContainer->GetChildrenBegin() != mpRowsContainer->GetChildrenEnd()) {
        mpRowsContainer->RemoveWindow(*mpRowsContainer->GetChildrenBegin());
    }

    float y = 2.0f;

    // Self row first (always shown when connected)
    if (selfConnected && !selfEmpireName.empty()) {
        // Green dot
        auto dot = MakePanel(4, y + 5, 14, y + 15, Math::Color(80, 220, 80, 255));
        mpRowsContainer->AddWindow(dot.get());
        // Name with "(you)" suffix
        eastl::string16 caption = ToStr16(selfEmpireName) + u" (you)";
        auto label = MakeLabel(18, y + 1, kPanelW - 6, y + kRowH - 1,
            caption.c_str(), Math::Color(200, 255, 200, 255));
        mpRowsContainer->AddWindow(label.get());
        y += kRowH;
    }

    if (players.empty() && !selfConnected) {
        auto label = MakeLabel(4, y, kPanelW - 6, y + kRowH,
            u"Not connected  [F9]", Math::Color(140, 140, 140, 255));
        mpRowsContainer->AddWindow(label.get());
        return;
    }

    if (players.empty()) {
        auto label = MakeLabel(4, y, kPanelW - 6, y + kRowH,
            u"No other players", Math::Color(140, 140, 140, 255));
        mpRowsContainer->AddWindow(label.get());
        return;
    }

    int shown = 0;
    for (auto& [id, emp] : players) {
        if (shown >= (int)kMaxRows - 1) {
            // Show overflow message
            eastl::string16 more = u"+" + ToStr16(std::to_string((int)players.size() - shown)) + u" more";
            auto label = MakeLabel(4, y, kPanelW - 6, y + kRowH,
                more.c_str(), Math::Color(140, 140, 140, 255));
            mpRowsContainer->AddWindow(label.get());
            break;
        }

        // Empire color swatch
        auto swatch = MakePanel(4, y + 5, 14, y + 15,
            Math::Color(emp.color[0], emp.color[1], emp.color[2], 255));
        mpRowsContainer->AddWindow(swatch.get());

        // Name label (dim if offline)
        Math::Color nameColor = emp.online
            ? Math::Color(220, 220, 220, 255)
            : Math::Color(100, 100, 100, 200);

        eastl::string16 caption = ToStr16(emp.name);
        if (!emp.online) caption += u" \u25CF"; // bullet = offline

        auto label = MakeLabel(18, y + 1, kPanelW - 6, y + kRowH - 1,
            caption.c_str(), nameColor);
        mpRowsContainer->AddWindow(label.get());

        // --- Star marker: find this empire's home star and set visual marker ---
        MarkHomeStar(emp);

        y += kRowH;
        ++shown;
    }
}

// ============================================================================
// Star marker: find cStarRecord by name, set its empire tint
// ============================================================================

void GalaxyOverlay::MarkHomeStar(const EmpireData& emp) {
    if (emp.homeWorld.empty()) return;

    auto* star = FindStarByName(emp.homeWorld);
    if (!star) return;

    // Set the star's empire flags so it renders with the remote player's color.
    // We use the star's TechLevel to indicate it's "owned" by an online player.
    // The mFlags field can carry kStarFlagHasPlayerEmpire to force a colored ring.
    //
    // Safely: only set if the star is currently unowned (mEmpireID == 0)
    // to avoid overwriting real game state.
    if (star->mEmpireID == 0) {
        // Encode player's empire color into the unused tech level bits
        // and set a marker flag. This is the least-invasive visual hack.
        // 0xFF000000 range of empireIDs should not conflict with real ones.
        uint32_t fakeId = 0xFF000000
            | ((uint32_t)emp.color[0] << 16)
            | ((uint32_t)emp.color[1] << 8)
            |  (uint32_t)emp.color[2];
        star->mEmpireID = fakeId;
    }
}

void GalaxyOverlay::UnmarkAllStars(const std::unordered_map<std::string, EmpireData>& players) {
    // Clear fake empire IDs we previously set (empireIDs in 0xFF000000 range)
    auto* starMgr = Simulator::StarManager();
    if (!starMgr) return;

    int count = starMgr->GetStarCount();
    for (int i = 0; i < count; ++i) {
        auto* rec = starMgr->GetStarRecord(i);
        if (rec && (rec->mEmpireID & 0xFF000000) == 0xFF000000) {
            rec->mEmpireID = 0;
        }
    }
}

Simulator::cStarRecord* GalaxyOverlay::FindStarByName(const std::string& name) {
    auto* starMgr = Simulator::StarManager();
    if (!starMgr) return nullptr;

    int count = starMgr->GetStarCount();
    for (int i = 0; i < count; ++i) {
        auto* rec = starMgr->GetStarRecord(i);
        if (!rec) continue;

        // Compare the star's name (string16) to our UTF-8 string
        auto& n16 = rec->GetName();
        if ((int)n16.size() != (int)name.size()) continue;

        bool match = true;
        for (size_t j = 0; j < name.size(); ++j) {
            if ((char)n16[j] != name[j]) { match = false; break; }
        }
        if (match) return rec;
    }
    return nullptr;
}

} // namespace OpenSpore
