#pragma once

#include <Spore\UTFWin\IWindow.h>
#include <Spore\UTFWin\Window.h>
#include <Spore\UTFWin\IWindowManager.h>
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>

#include "../net/ApiClient.h"

#include <string>
#include <unordered_map>

namespace OpenSpore {

/// Always-visible HUD panel (top-right) showing online players.
/// Also marks remote players' home stars via cStarRecord::mEmpireID.
class GalaxyOverlay {
public:
    GalaxyOverlay();
    ~GalaxyOverlay();

    void Show();
    void Hide();

    void Update(const std::unordered_map<std::string, EmpireData>& players,
                bool selfConnected, const std::string& selfEmpireName);

    static void UnmarkAllStars(const std::unordered_map<std::string, EmpireData>& players);

private:
    void Build();
    void RebuildPlayerRows(const std::unordered_map<std::string, EmpireData>& players,
                           bool selfConnected, const std::string& selfEmpireName);

    static Simulator::cStarRecord* FindStarByName(const std::string& name);
    static void MarkHomeStar(const EmpireData& emp);

    static constexpr float kPanelW  = 230.0f;
    static constexpr float kHeaderH = 28.0f;
    static constexpr float kRowH    = 22.0f;
    static constexpr float kMaxRows = 8.0f;
    static constexpr float kPadR    = 10.0f;
    static constexpr float kPadT    = 56.0f;

    WindowPtr mpRoot;
    WindowPtr mpRowsContainer;

    bool mBuilt   = false;
    bool mVisible = false;
};

} // namespace OpenSpore
