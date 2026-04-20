/**
 * @file AppDelegate.mm
 * @brief iOS 应用程序代理实现
 */

#import "AppDelegate.h"
#import "PlayerViewController.h"
#import "../HXCVDownload/HXCVDownload.h"
#import "../HXCPlayerLicenseManager.h"
#define HXCVOD_LICENSE_URL @"https://console-api.huaxiacloud.net/license/getMobileLicense/111453136245362688"
#define HXCVOD_LICENSE_KEY @"JNlhoUFDoLeDwNJEcoCS4GxAWk3Z2b8K"

@implementation AppDelegate

- (void)application:(UIApplication *)application handleEventsForBackgroundURLSession:(NSString *)identifier completionHandler:(void (^)(void))completionHandler {
    [HXCVDownloadManager notifyBackgroundURLSessionEventsForIdentifier:identifier completionHandler:completionHandler];
}

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
    
    
    [self checkLicense];
    return YES;
}

-(void)checkLicense {
    [HXCPlayerLicenseManager checkLicenseWithLicenseKey:HXCVOD_LICENSE_KEY licenseURL:HXCVOD_LICENSE_URL completionHandler:^(BOOL success, NSError * _Nullable error) {
        if (error) {
            NSLog(@"license check faild...");
        }
    }];
}

@end
