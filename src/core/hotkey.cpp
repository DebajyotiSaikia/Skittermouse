#include "core/hotkey.h"

#include <vector>

namespace sm::core {

namespace {

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// Map a non-modifier token to a virtual-key code. Returns 0 if unrecognized.
uint16_t keyFor(const std::string& t) {
    if (t.size() == 1) {
        char c = t[0];
        if (c >= 'a' && c <= 'z') return static_cast<uint16_t>('A' + (c - 'a')); // VK 0x41..
        if (c >= '0' && c <= '9') return static_cast<uint16_t>('0' + (c - '0')); // VK 0x30..
    }
    if (t == "space") return 0x20;
    if (t == "enter" || t == "return") return 0x0D;
    if (t == "tab") return 0x09;
    if (t == "esc" || t == "escape") return 0x1B;
    if (t == "backspace") return 0x08;
    if (t == "delete" || t == "del") return 0x2E;
    if (t == "insert" || t == "ins") return 0x2D;
    if (t == "home") return 0x24;
    if (t == "end") return 0x23;
    if (t == "pageup" || t == "pgup") return 0x21;
    if (t == "pagedown" || t == "pgdn") return 0x22;
    if (t == "left") return 0x25;
    if (t == "up") return 0x26;
    if (t == "right") return 0x27;
    if (t == "down") return 0x28;
    if (t.size() >= 2 && t[0] == 'f') {
        int n = 0;
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (t[i] < '0' || t[i] > '9') return 0;
            n = n * 10 + (t[i] - '0');
        }
        if (n >= 1 && n <= 24) return static_cast<uint16_t>(0x70 + (n - 1)); // VK_F1..VK_F24
    }
    return 0;
}

} // namespace

Hotkey parseHotkey(const std::string& s) {
    Hotkey hk;
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : s) {
        if (c == '+') {
            tokens.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    tokens.push_back(cur);

    bool haveKey = false;
    for (const std::string& raw : tokens) {
        std::string t = lower(trim(raw));
        if (t.empty()) return Hotkey{}; // stray '+' or blank token
        if (t == "ctrl" || t == "control") { hk.modifiers |= hotkey_mod::Control; continue; }
        if (t == "alt" || t == "option" || t == "opt") { hk.modifiers |= hotkey_mod::Alt; continue; }
        if (t == "shift") { hk.modifiers |= hotkey_mod::Shift; continue; }
        if (t == "win" || t == "cmd" || t == "super" || t == "meta") { hk.modifiers |= hotkey_mod::Win; continue; }

        uint16_t vk = keyFor(t);
        if (vk == 0) return Hotkey{}; // unrecognized token
        if (haveKey) return Hotkey{}; // more than one non-modifier key
        hk.key = vk;
        haveKey = true;
    }

    hk.valid = haveKey;
    return hk;
}

} // namespace sm::core
