#include "ui/menu_model.h"

#include <algorithm>

namespace sm::ui {

std::vector<MenuItem> buildMachineMenu(const std::vector<sm::core::PairedDevice>& devices,
                                       const sm::core::PeerId& self,
                                       const sm::core::PeerId& owner,
                                       const std::vector<sm::core::PeerId>& online) {
    auto isOnline = [&](const sm::core::PeerId& id) {
        return std::find(online.begin(), online.end(), id) != online.end();
    };

    std::vector<MenuItem> items;
    items.reserve(devices.size());
    for (const auto& d : devices) {
        MenuItem m;
        m.id = d.id;
        m.name = d.name;
        m.is_self = (d.id == self);
        m.is_owner = (d.id == owner);
        // This machine is always reachable to itself; others per the online set.
        m.is_online = m.is_self || isOnline(d.id);
        items.push_back(m);
    }
    return items;
}

} // namespace sm::ui
