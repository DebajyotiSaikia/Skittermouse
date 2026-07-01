#include "test_framework.h"

#include "core/key_translation.h"

using namespace sm::core;

void run_key_translation_tests() {
    // Same-OS pairs: identity for everything (Section 4.5).
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::Windows, win_vk::LWin), win_vk::LWin);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::MacOS, mac_vk::Command), mac_vk::Command);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::Windows, 0x41), 0x41); // 'A'

    // Windows -> macOS, by physical slot.
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::LControl), mac_vk::Control);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::RControl), mac_vk::RControl);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::LWin), mac_vk::Option);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::RWin), mac_vk::ROption);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::LAlt), mac_vk::Command);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::RAlt), mac_vk::RCommand);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::LShift), mac_vk::Shift);
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, win_vk::RShift), mac_vk::RShift);

    // macOS -> Windows: exact inverse.
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::Control), win_vk::LControl);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::RControl), win_vk::RControl);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::Option), win_vk::LWin);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::ROption), win_vk::RWin);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::Command), win_vk::LAlt);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::RCommand), win_vk::RAlt);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::Shift), win_vk::LShift);
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, mac_vk::RShift), win_vk::RShift);

    // Round-trip: Win -> Mac -> Win is identity for every modifier.
    const uint8_t win_mods[] = {win_vk::LControl, win_vk::RControl, win_vk::LWin,
                                win_vk::RWin, win_vk::LAlt, win_vk::RAlt,
                                win_vk::LShift, win_vk::RShift};
    for (uint8_t m : win_mods) {
        uint8_t there = translateModifier(Os::Windows, Os::MacOS, m);
        uint8_t back = translateModifier(Os::MacOS, Os::Windows, there);
        SM_CHECK_EQ(back, m);
    }

    // Non-modifier keys pass through unchanged across OSes.
    SM_CHECK_EQ(translateModifier(Os::Windows, Os::MacOS, 0x41), 0x41); // 'A'
    SM_CHECK_EQ(translateModifier(Os::MacOS, Os::Windows, 0x00), 0x00);
}
