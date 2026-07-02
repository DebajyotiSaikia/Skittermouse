// macOS tray application shell (spec 10). An NSStatusBar item with a menu, hosting
// the app; runs as an accessory (no Dock icon). Native AppKit. The mesh/hotkey/
// clipboard wiring behind it mirrors the Windows tray as those pieces land.

#include "platform/tray_app.h"

#include "core/config.h"
#include "core/hotkey.h"
#include "platform/global_hotkey.h"
#include "ui/menu_model.h"
#include "ui/picker_window.h"

#import <AppKit/AppKit.h>

#include <string>

@interface SMTrayDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSStatusItem* statusItem;
- (void)openPicker;
@end

@implementation SMTrayDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    self.statusItem =
        [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    self.statusItem.button.title = @"Skittermouse";

    NSMenu* menu = [[NSMenu alloc] init];

    sm::core::Config config; // paired-device list would be loaded here
    auto items = sm::ui::buildMachineMenu(config.devices, "this-machine", "this-machine", {});
    if (items.empty()) {
        NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"No devices paired"
                                                      action:nil
                                               keyEquivalent:@""];
        [menu addItem:none];
    } else {
        for (const auto& it : items) {
            NSString* title = [NSString stringWithUTF8String:it.name.c_str()];
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:title ? title : @"?"
                                                        action:nil
                                                 keyEquivalent:@""];
            if (it.is_owner) mi.state = NSControlStateValueOn;
            if (!it.is_online) [mi setEnabled:NO];
            [menu addItem:mi];
        }
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit Skittermouse"
                                                  action:@selector(quit:)
                                           keyEquivalent:@"q"];
    quit.target = self;
    [menu addItem:quit];

    self.statusItem.menu = menu;

    // Global hotkey -> picker, mirroring the Windows tray (spec 4.1/4.2). If the
    // configured combo is already owned by another app, fall back to the secondary
    // combo, same policy as tray_win.cpp.
    SMTrayDelegate* me = self;
    sm::core::Hotkey hk = sm::core::parseHotkey(config.settings.hotkey);
    if (!sm::platform::registerGlobalHotkey(hk, [me]() { [me openPicker]; })) {
        sm::core::Hotkey fallback = sm::core::parseHotkey("Ctrl+Shift+Alt+Space");
        sm::platform::registerGlobalHotkey(fallback, [me]() { [me openPicker]; });
    }
}

- (void)openPicker {
    // Same list/logic as the tray menu (spec 4.3). Selecting a machine is where a
    // switch would be requested once the macOS mesh/connection layer is wired in,
    // mirroring requestSwitchTo() in the Windows tray.
    sm::core::Config config;
    auto items = sm::ui::buildMachineMenu(config.devices, "this-machine", "this-machine", {});
    std::string chosen = sm::ui::showPicker(items);
    if (!chosen.empty()) {
        NSString* title = [NSString stringWithUTF8String:chosen.c_str()];
        self.statusItem.button.title = title ? title : @"Skittermouse";
    }
}

- (void)quit:(id)sender {
    [NSApp terminate:sender];
}

@end

namespace sm::platform {

int runTrayApp() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory]; // no Dock icon
        SMTrayDelegate* delegate = [[SMTrayDelegate alloc] init];
        app.delegate = delegate;
        [app run];
        return 0;
    }
}

} // namespace sm::platform
