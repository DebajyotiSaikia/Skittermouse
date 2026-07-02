#pragma once

// Shared machine-list model for the tray menu and the hotkey picker (spec 10, 4.3
// -- "same list/logic"). PURE LOGIC: given the paired devices, who we are, the
// current input owner, and the online set, produce the display rows. Unreachable
// machines are INCLUDED but flagged offline (greyed, not omitted -- spec 4.2), so
// the user sees why they can't switch there.

#include <string>
#include <vector>

#include "core/config.h"
#include "core/peer_id.h"

namespace sm::ui {

struct MenuItem {
    std::string id;
    std::string name;
    bool is_owner = false;  // currently holds input -> marked in the UI
    bool is_online = false; // reachable; offline rows are shown disabled/greyed
    bool is_self = false;   // this machine
};

std::vector<MenuItem> buildMachineMenu(const std::vector<sm::core::PairedDevice>& devices,
                                       const sm::core::PeerId& self,
                                       const sm::core::PeerId& owner,
                                       const std::vector<sm::core::PeerId>& online);

} // namespace sm::ui
