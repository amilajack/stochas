// SPDX-License-Identifier: GPL-3.0-only
// Restrict only this oracle process; never change the user's application focus.
#import <AppKit/AppKit.h>
#include <unistd.h>
extern "C" bool stochas_oracle_background_only() {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
        return application.activationPolicy == NSApplicationActivationPolicyProhibited &&
            NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier != getpid();
    }
}
extern "C" bool stochas_oracle_never_frontmost() {
    @autoreleasepool {
        return NSApp.activationPolicy == NSApplicationActivationPolicyProhibited &&
            NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier != getpid();
    }
}
