/**
 * @file main.mm
 * @brief macOS 应用程序入口
 */

#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        
        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        
        [app run];
    }
    return 0;
}
