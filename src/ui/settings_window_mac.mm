// macOS native settings window (spec 10/16). A small modal Cocoa window with a
// hotkey field and checkboxes for the boolean settings; Save writes them back into
// the Config. Native AppKit, zero third-party. Mirrors ui/settings_window_win.cpp.

#include "ui/settings_window.h"

#import <AppKit/AppKit.h>

#include <string>

@interface SMSettingsController : NSObject <NSWindowDelegate>
@property(assign) sm::core::Config* config;
@property(strong) NSTextField* hotkeyField;
@property(strong) NSButton* broadcastBox;
@property(strong) NSButton* lockBox;
@property(strong) NSButton* startupBox;
@property(assign) BOOL saved;
- (void)save:(id)sender;
- (void)cancel:(id)sender;
@end

@implementation SMSettingsController

- (void)save:(id)sender {
    (void)sender;
    if (self.config) {
        const char* hk = [[self.hotkeyField stringValue] UTF8String];
        self.config->settings.hotkey = hk ? std::string(hk) : std::string();
        self.config->settings.broadcast_presence =
            (self.broadcastBox.state == NSControlStateValueOn);
        self.config->settings.lock_propagation_optin =
            (self.lockBox.state == NSControlStateValueOn);
        self.config->settings.run_on_startup = (self.startupBox.state == NSControlStateValueOn);
    }
    self.saved = YES;
    [NSApp stopModalWithCode:1];
}

- (void)cancel:(id)sender {
    (void)sender;
    self.saved = NO;
    [NSApp stopModalWithCode:0];
}

- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    self.saved = NO;
    [NSApp stopModalWithCode:0];
    return YES;
}

@end

namespace sm::ui {

bool showSettingsWindow(sm::core::Config& config) {
    @autoreleasepool {
        NSWindow* win = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 360, 210)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        win.title = @"Skittermouse Settings";
        [win center];
        NSView* v = win.contentView;

        SMSettingsController* ctl = [[SMSettingsController alloc] init];
        ctl.config = &config;
        win.delegate = ctl;

        NSTextField* label = [NSTextField labelWithString:@"Switch hotkey:"];
        label.frame = NSMakeRect(16, 168, 110, 20);
        [v addSubview:label];

        NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(130, 166, 210, 24)];
        NSString* hk = [NSString stringWithUTF8String:config.settings.hotkey.c_str()];
        field.stringValue = hk ? hk : @"";
        [v addSubview:field];
        ctl.hotkeyField = field;

        auto makeCheck = [&](NSString* title, CGFloat y, bool on) -> NSButton* {
            NSButton* b = [NSButton checkboxWithTitle:title target:nil action:nil];
            b.frame = NSMakeRect(16, y, 328, 22);
            b.state = on ? NSControlStateValueOn : NSControlStateValueOff;
            [v addSubview:b];
            return b;
        };
        ctl.broadcastBox = makeCheck(@"Announce this Mac on the LAN (discovery)", 130,
                                     config.settings.broadcast_presence);
        ctl.lockBox = makeCheck(@"Allow this Mac to be locked remotely", 102,
                                config.settings.lock_propagation_optin);
        ctl.startupBox =
            makeCheck(@"Run Skittermouse on startup", 74, config.settings.run_on_startup);

        NSButton* save = [NSButton buttonWithTitle:@"Save" target:ctl action:@selector(save:)];
        save.frame = NSMakeRect(178, 16, 82, 30);
        save.keyEquivalent = @"\r";
        [v addSubview:save];

        NSButton* cancel = [NSButton buttonWithTitle:@"Cancel"
                                              target:ctl
                                              action:@selector(cancel:)];
        cancel.frame = NSMakeRect(262, 16, 82, 30);
        [v addSubview:cancel];

        [NSApp activateIgnoringOtherApps:YES];
        [win makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:win];
        [win orderOut:nil];
        return ctl.saved ? true : false;
    }
}

} // namespace sm::ui
