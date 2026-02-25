/**
 * @file AppDelegate.mm
 * @brief macOS 应用程序代理实现
 */

#import "AppDelegate.h"
#import "PlayerViewController.h"

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // 创建主窗口
    NSRect contentRect = NSMakeRect(0, 0, 1280, 720);
    NSWindowStyleMask styleMask = NSWindowStyleMaskTitled |
                                  NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable |
                                  NSWindowStyleMaskResizable;
    
    self.window = [[NSWindow alloc] initWithContentRect:contentRect
                                              styleMask:styleMask
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    
    [self.window setTitle:@"HXC Player - macOS"];
    [self.window center];
    
    // 创建播放器视图控制器
    PlayerViewController *viewController = [[PlayerViewController alloc] init];
    self.window.contentViewController = viewController;
    
    [self.window makeKeyAndOrderFront:nil];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // 应用即将退出
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

@end
