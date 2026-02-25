/**
 * @file AppDelegate.mm
 * @brief iOS 应用程序代理实现
 */

#import "AppDelegate.h"
#import "PlayerViewController.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // 创建窗口
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor whiteColor];
    
    // 创建播放器视图控制器
    PlayerViewController *playerVC = [[PlayerViewController alloc] init];
    
    // 设置根视图控制器
    self.window.rootViewController = playerVC;
    
    // 显示窗口
    [self.window makeKeyAndVisible];
    
    return YES;
}

@end
