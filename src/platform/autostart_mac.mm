// macOS auto-start at login via a LaunchAgent plist (spec 13). Writes
// ~/Library/LaunchAgents/com.skittermouse.agent.plist pointing at this executable.

#include "platform/autostart.h"

#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>

#include <string>
#include <vector>

namespace sm::platform {

namespace {

NSString* plistPath() {
    return [NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/LaunchAgents/com.skittermouse.agent.plist"];
}

std::string exePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, 0);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    return std::string(buf.data());
}

} // namespace

bool enableAutostart() {
    @autoreleasepool {
        NSString* exe = [NSString stringWithUTF8String:exePath().c_str()];
        if (!exe) return false;
        NSDictionary* plist = @{
            @"Label" : @"com.skittermouse.agent",
            @"ProgramArguments" : @[ exe ],
            @"RunAtLoad" : @YES
        };
        NSString* dir =
            [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        return [plist writeToFile:plistPath() atomically:YES];
    }
}

bool disableAutostart() {
    @autoreleasepool {
        return [[NSFileManager defaultManager] removeItemAtPath:plistPath() error:nil];
    }
}

bool isAutostartEnabled() {
    @autoreleasepool {
        return [[NSFileManager defaultManager] fileExistsAtPath:plistPath()];
    }
}

} // namespace sm::platform
