// macOS pairing confirmation dialog (spec 7.1). Native AppKit NSAlert.

#include "pairing/pairing_dialog.h"

#import <AppKit/AppKit.h>

namespace sm::platform {

bool confirmPairingCode(const std::string& code, const std::string& peerName) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Skittermouse pairing";
        NSString* peer = [NSString stringWithUTF8String:peerName.c_str()];
        NSString* c = [NSString stringWithUTF8String:code.c_str()];
        alert.informativeText =
            [NSString stringWithFormat:@"Confirm this code matches the one shown on %@:\n\n"
                                       @"        %@\n\nDo the codes match?",
                                       peer ? peer : @"the other machine", c ? c : @""];
        [alert addButtonWithTitle:@"Match"];
        [alert addButtonWithTitle:@"Reject"];
        return [alert runModal] == NSAlertFirstButtonReturn;
    }
}

} // namespace sm::platform
