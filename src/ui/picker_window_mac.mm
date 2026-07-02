// macOS hotkey picker window (spec 4.2). A topmost, key-focus-stealing borderless
// NSPanel listing the paired machines (owner marked, offline greyed), navigated by
// Up/Down and confirmed with Enter; Esc or a click outside dismisses it. Mirrors the
// Windows picker's behaviour and returns the chosen machine id (or "" if dismissed).
// Native AppKit, zero third-party.

#include "ui/picker_window.h"

#import <AppKit/AppKit.h>

#include <string>
#include <vector>

namespace {

// A borderless panel normally refuses key focus; the picker must own the keyboard
// for its lifetime (spec 4.2), so allow it.
} // namespace

@interface SMPickerPanel : NSPanel
@end
@implementation SMPickerPanel
- (BOOL)canBecomeKeyWindow {
    return YES;
}
@end

@interface SMPickerView : NSView {
@public
    const std::vector<sm::ui::MenuItem>* items;
    int selection;
}
@end

@implementation SMPickerView

- (BOOL)isFlipped {
    return YES; // draw top-down, like the Win32 picker
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor colorWithCalibratedRed:0.06 green:0.06 blue:0.06 alpha:0.98] setFill];
    NSRectFill(self.bounds);
    if (!items) return;

    NSFont* font = [NSFont systemFontOfSize:14];
    CGFloat y = 8;
    for (int i = 0; i < static_cast<int>(items->size()); ++i) {
        const sm::ui::MenuItem& it = (*items)[i];
        NSRect row = NSMakeRect(6, y, self.bounds.size.width - 12, 24);
        if (i == selection) {
            [[NSColor colorWithCalibratedRed:0.23 green:0.35 blue:0.63 alpha:1.0] setFill];
            NSRectFill(row);
        }
        NSColor* fg = it.is_online ? [NSColor colorWithWhite:0.94 alpha:1.0]
                                   : [NSColor colorWithWhite:0.47 alpha:1.0];
        NSString* marker = it.is_owner ? @"\u25CF " : @"   ";
        NSString* name = [NSString stringWithUTF8String:it.name.c_str()];
        if (!name) name = @"?";
        NSString* label = [marker stringByAppendingString:name];
        NSDictionary* attrs = @{
            NSFontAttributeName : font,
            NSForegroundColorAttributeName : fg
        };
        [label drawAtPoint:NSMakePoint(14, y + 3) withAttributes:attrs];
        y += 26;
    }
}

@end

namespace sm::ui {

std::string showPicker(const std::vector<MenuItem>& items) {
    @autoreleasepool {
        // Start on the current owner if it's online, else the first online machine.
        int sel = -1;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].is_owner && items[i].is_online) {
                sel = static_cast<int>(i);
                break;
            }
        }
        if (sel < 0) {
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (items[i].is_online) {
                    sel = static_cast<int>(i);
                    break;
                }
            }
        }
        if (sel < 0) sel = 0;

        CGFloat width = 300;
        CGFloat height = static_cast<CGFloat>(items.size()) * 26 + 16;
        if (height < 60) height = 60;

        NSRect screen = [[NSScreen mainScreen] frame];
        NSRect frame = NSMakeRect((screen.size.width - width) / 2,
                                  (screen.size.height - height) / 2, width, height);

        SMPickerPanel* panel =
            [[SMPickerPanel alloc] initWithContentRect:frame
                                             styleMask:NSWindowStyleMaskBorderless
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        panel.level = NSFloatingWindowLevel; // topmost (spec 4.2)
        panel.opaque = NO;
        panel.hidesOnDeactivate = NO;
        [panel setBackgroundColor:[NSColor clearColor]];

        SMPickerView* view =
            [[SMPickerView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
        view->items = &items;
        view->selection = sel;
        panel.contentView = view;

        [NSApp activateIgnoringOtherApps:YES];
        [panel makeKeyAndOrderFront:nil];

        std::string result;
        bool done = false;

        auto moveSel = [&](int dir) {
            int n = static_cast<int>(items.size());
            if (n == 0) return;
            int s = view->selection;
            for (int i = 0; i < n; ++i) {
                s = (s + dir + n) % n;
                if (items[s].is_online) { // skip offline (unselectable) rows
                    view->selection = s;
                    return;
                }
            }
        };

        while (!done) {
            NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantFuture]
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) continue;

            if (ev.type == NSEventTypeKeyDown) {
                switch (ev.keyCode) {
                    case 53: // Escape
                        result.clear();
                        done = true;
                        break;
                    case 126: // Up arrow
                        moveSel(-1);
                        [view setNeedsDisplay:YES];
                        break;
                    case 125: // Down arrow
                        moveSel(1);
                        [view setNeedsDisplay:YES];
                        break;
                    case 36:  // Return
                    case 76: {// Enter (keypad)
                        int s = view->selection;
                        if (s >= 0 && s < static_cast<int>(items.size()) &&
                            items[s].is_online) {
                            result = items[s].id;
                        }
                        done = true;
                        break;
                    }
                    default:
                        break;
                }
                continue; // swallow keys (don't forward to the app underneath)
            }

            if (ev.type == NSEventTypeLeftMouseDown && [ev window] != panel) {
                result.clear(); // click-away dismisses
                done = true;
                continue;
            }

            [NSApp sendEvent:ev];
        }

        [panel orderOut:nil];
        return result;
    }
}

} // namespace sm::ui
