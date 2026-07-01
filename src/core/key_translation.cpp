#include "core/key_translation.h"

namespace sm::core {

uint8_t translateModifier(Os from, Os to, uint8_t code) {
    if (from == to) return code; // same-OS pairs: identity (Section 4.5)

    if (from == Os::Windows && to == Os::MacOS) {
        switch (code) {
            case win_vk::LControl: return mac_vk::Control;
            case win_vk::RControl: return mac_vk::RControl;
            case win_vk::LWin:     return mac_vk::Option;   // Win slot -> Option slot
            case win_vk::RWin:     return mac_vk::ROption;
            case win_vk::LAlt:     return mac_vk::Command;  // Alt slot -> Cmd slot
            case win_vk::RAlt:     return mac_vk::RCommand;
            case win_vk::LShift:   return mac_vk::Shift;
            case win_vk::RShift:   return mac_vk::RShift;
            default:               return code;             // not a modifier: passthrough
        }
    }

    // Os::MacOS -> Os::Windows (exact inverse of the mapping above).
    switch (code) {
        case mac_vk::Control:  return win_vk::LControl;
        case mac_vk::RControl: return win_vk::RControl;
        case mac_vk::Option:   return win_vk::LWin;         // Option slot -> Win slot
        case mac_vk::ROption:  return win_vk::RWin;
        case mac_vk::Command:  return win_vk::LAlt;         // Cmd slot -> Alt slot
        case mac_vk::RCommand: return win_vk::RAlt;
        case mac_vk::Shift:    return win_vk::LShift;
        case mac_vk::RShift:   return win_vk::RShift;
        default:               return code;                 // not a modifier: passthrough
    }
}

} // namespace sm::core
