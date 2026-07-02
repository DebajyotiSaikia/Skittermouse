// macOS plain-text clipboard via NSPasteboard (spec 8). Polling for changes lives
// in the app loop; this is the get/set + password-manager exclusion check.

#include "platform/clipboard.h"

#import <AppKit/AppKit.h>

namespace sm::platform {

bool getClipboardText(std::string& utf8Out) {
    @autoreleasepool {
        NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (!s) return false;
        const char* c = [s UTF8String];
        utf8Out = c ? std::string(c) : std::string();
        return true;
    }
}

bool setClipboardText(const std::string& utf8) {
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
        return s && [pb setString:s forType:NSPasteboardTypeString];
    }
}

bool clipboardExcludedFromMonitoring() {
    @autoreleasepool {
        // Password managers tag writes as concealed/transient (org.nspasteboard.*).
        for (NSString* t in [[NSPasteboard generalPasteboard] types]) {
            if ([t isEqualToString:@"org.nspasteboard.ConcealedType"] ||
                [t isEqualToString:@"org.nspasteboard.TransientType"]) {
                return true;
            }
        }
        return false;
    }
}

} // namespace sm::platform
