// macOS tray application shell (spec 10). An NSStatusBar item with a menu, hosting
// the app; runs as an accessory (no Dock icon). Native AppKit. The mesh/hotkey/
// clipboard wiring behind it mirrors the Windows tray as those pieces land.

#include "platform/tray_app.h"

#include "core/config.h"
#include "ui/menu_model.h"

#import <AppKit/AppKit.h>

#include <string>

@interface SMTrayDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSStatusItem* statusItem;
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
