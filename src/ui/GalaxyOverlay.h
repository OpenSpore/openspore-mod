#pragma once

#include <Spore\UTFWin\IWindow.h>
#include <Spore\UTFWin\Window.h>
#include <Spore\UTFWin\IWindowManager.h>
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>

#include "../net/ApiClient.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace OpenSpore {

using namespace UTFWin;

/// Always-visible HUD panel (top-right corner) showing online players.
/// Also marks home stars of remote players in the galaxy by setting their
/// empire tint color via cStarRecord::mEmpireID / color manipulation.
class GalaxyOverlay {
public:
    GalaxyOverlay();
    ~GalaxyOverlay();

    void Show();
    void Hide();

    /// Rebuild the player list UI rows and refresh star markers.
    void Update(const std::unordered_map<std::string, EmpireData>& players,
                bool selfConnected, const std::string& selfEmpireName);

private:
    void Build();
    void RebuildPlayerRows(const std::unordered_map<std::string, EmpireData>& players,
                           bool selfConnected, const std::string& selfEmpireName);

    /// Find a cStarRecord by its name (UTF-8).
    static Simulator::cStarRecord* FindStarByName(const std::string& name);

    /// Set a colored marker on a remote player's home star.
    static void MarkHomeStar(const EmpireData& emp);

    /// Remove all fake empire IDs we previously injected.
    static void UnmarkAllStars(const std::unordered_map<std::string, EmpireData>& players);

    // Layout constants (top-right anchor)
    static constexpr float kPanelW  = 230.0f;
    static constexpr float kHeaderH = 28.0f;
    static constexpr float kRowH    = 22.0f;
    static constexpr float kMaxRows = 8.0f;
    static constexpr float kPadR    = 10.0f; // right margin from 1024
    static constexpr float kPadT    = 56.0f; // top margin (below Spore's HUD bar)

    WindowPtr mpRoot;        // outer panel
    WindowPtr mpRowsContainer; // inner container for player rows

    bool mBuilt   = false;
    bool mVisible = false;
};

} // namespace OpenSpore
