/**
 * @file PlayerViewController.mm
 * @brief macOS 播放器视图控制器实现
 */

#import "PlayerViewController.h"
#import "../HXCPlayerControl.h"  // 使用统一的播放器类
#import <dispatch/dispatch.h>
#import "SeekSlider.h"
#import "../HXCAESUtility.h"
#import "HXCVDownloadWindowController.h"
#import "../HXCVDownload/HXCVDownload.h"
#import "../HXCPlayerLicenseManager.h"

#define HXCVODPLAYER_LICENSE_URL    @"https://console-api.huaxiacloud.net/license/getMobileLicense/111453136245362688"
#define HXCVODPLAYER_LICENSE_KEY    @"JNlhoUFDoLeDwNJEcoCS4GxAWk3Z2b8K"

@interface PlayerViewController () <HXCPlayerControlDelegate, SeekSliderDelegate, NSTableViewDataSource, NSTableViewDelegate>

@property (nonatomic, strong) HXCPlayerControl *player;
@property (nonatomic, strong) HXCVDownloadWindowController *downloadWindowController;

// UI 控件
@property (nonatomic, strong) NSButton *openButton;
@property (nonatomic, strong) NSButton *playPauseButton;
@property (nonatomic, strong) NSButton *stopButton;
@property (nonatomic, strong) NSButton *aspectRatioButton;
@property (nonatomic, strong) NSButton *downloadButton;
@property (nonatomic, strong) NSButton *completedDownloadsButton;
@property (nonatomic, strong) NSPopover *completedDownloadsPopover;
@property (nonatomic, copy) NSArray<HXCVDownloadItem *> *completedDownloadsItems;
@property (nonatomic, strong) NSTableView *completedDownloadsTableView;
@property (nonatomic, strong) SeekSlider *progressSlider;
@property (nonatomic, strong) NSSlider *volumeSlider;
@property (nonatomic, strong) NSPopUpButton *speedButton;
@property (nonatomic, strong) NSTextField *timeLabel;

@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation PlayerViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 720)];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    
//    NSString *key = @"12345678901234567890123456789012"; // 32位
//    NSString *iv  = @"1234567890123456"; // 16位
//    NSString *text = @"测试跨平台加密";
//
//    NSString *encrypted = [HXCAESUtility encryptString:text key:key iv:iv];
//    NSLog(@"加密：%@", encrypted);
//
//    NSString *decrypted = [HXCAESUtility decryptString:encrypted key:key iv:iv];
//    NSLog(@"解密：%@", decrypted);
//    [HXCPlayerLicenseManager setPlaybackLicenseGateEnabled:YES];
    [HXCPlayerLicenseManager checkLicenseWithLicenseKey:HXCVODPLAYER_LICENSE_KEY licenseURL:HXCVODPLAYER_LICENSE_URL completionHandler:^(BOOL success, NSError * _Nullable error) {
        NSLog(@"error: %@", error);
    }];
    [self setupPlayer];
    [self setupUI];
}

- (void)dealloc {
    [_player stop];
}

#pragma mark - Setup

- (void)setupPlayer {
    // 创建播放器
    _player = [[HXCPlayerControl alloc] init];
    _player.startPosition = 60;
    _player.delegate = self;
    // 添加视频视图
    [self.view addSubview:_player.videoView];
    
    // 设置自动布局
    _player.videoView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_player.videoView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_player.videoView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_player.videoView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_player.videoView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-100]
    ]];
}

- (void)setupUI {
    // 控制栏背景
    NSBox *controlBar = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 1280, 100)];
    controlBar.boxType = NSBoxCustom;
    controlBar.fillColor = [NSColor colorWithWhite:0.2 alpha:1.0];
    controlBar.borderColor = [NSColor clearColor];
    [self.view addSubview:controlBar];
    
    controlBar.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [controlBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [controlBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [controlBar.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [controlBar.heightAnchor constraintEqualToConstant:100]
    ]];
    
    // 进度条
    _progressSlider = [[SeekSlider alloc] initWithFrame:NSMakeRect(20, 70, 1240, 20)];
    _progressSlider.minValue = 0;
    _progressSlider.maxValue = 1000;
    _progressSlider.continuous = YES;
    _progressSlider.seekDelegate = self;
    [controlBar addSubview:_progressSlider];
    
    _progressSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_progressSlider.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:10],
        [_progressSlider.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:20],
        [_progressSlider.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-20],
        [_progressSlider.heightAnchor constraintEqualToConstant:20]
    ]];
    
    // 按钮容器
    NSView *buttonContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 60)];
    [controlBar addSubview:buttonContainer];
    
    buttonContainer.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [buttonContainer.topAnchor constraintEqualToAnchor:_progressSlider.bottomAnchor constant:10],
        [buttonContainer.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor],
        [buttonContainer.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor],
        [buttonContainer.bottomAnchor constraintEqualToAnchor:controlBar.bottomAnchor constant:-10]
    ]];
    
    // 打开按钮
    _openButton = [[NSButton alloc] initWithFrame:NSMakeRect(20, 20, 80, 30)];
    _openButton.title = @"打开";
    _openButton.bezelStyle = NSBezelStyleRounded;
    _openButton.target = self;
    _openButton.action = @selector(openButtonClicked:);
    [buttonContainer addSubview:_openButton];
    
    _openButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_openButton.leadingAnchor constraintEqualToAnchor:buttonContainer.leadingAnchor constant:20],
        [_openButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_openButton.widthAnchor constraintEqualToConstant:80],
        [_openButton.heightAnchor constraintEqualToConstant:30]
    ]];
    
    // 播放/暂停按钮
    _playPauseButton = [[NSButton alloc] initWithFrame:NSMakeRect(110, 20, 80, 30)];
    _playPauseButton.title = @"播放";
    _playPauseButton.bezelStyle = NSBezelStyleRounded;
    _playPauseButton.target = self;
    _playPauseButton.action = @selector(playPauseButtonClicked:);
    _playPauseButton.enabled = NO;
    [buttonContainer addSubview:_playPauseButton];
    
    _playPauseButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_playPauseButton.leadingAnchor constraintEqualToAnchor:_openButton.trailingAnchor constant:10],
        [_playPauseButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_playPauseButton.widthAnchor constraintEqualToConstant:80],
        [_playPauseButton.heightAnchor constraintEqualToConstant:30]
    ]];
    
    // 停止按钮
    _stopButton = [[NSButton alloc] initWithFrame:NSMakeRect(200, 20, 80, 30)];
    _stopButton.title = @"停止";
    _stopButton.bezelStyle = NSBezelStyleRounded;
    _stopButton.target = self;
    _stopButton.action = @selector(stopButtonClicked:);
    _stopButton.enabled = NO;
    [buttonContainer addSubview:_stopButton];
    
    _stopButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_stopButton.leadingAnchor constraintEqualToAnchor:_playPauseButton.trailingAnchor constant:10],
        [_stopButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_stopButton.widthAnchor constraintEqualToConstant:80],
        [_stopButton.heightAnchor constraintEqualToConstant:30]
    ]];
    
    // 时间标签
    _timeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(290, 20, 120, 30)];
    _timeLabel.stringValue = @"00:00 / 00:00";
    _timeLabel.editable = NO;
    _timeLabel.bordered = NO;
    _timeLabel.backgroundColor = [NSColor clearColor];
    _timeLabel.textColor = [NSColor whiteColor];
    _timeLabel.alignment = NSTextAlignmentCenter;
    [buttonContainer addSubview:_timeLabel];
    
    _timeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_timeLabel.leadingAnchor constraintEqualToAnchor:_stopButton.trailingAnchor constant:10],
        [_timeLabel.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_timeLabel.widthAnchor constraintEqualToConstant:120],
        [_timeLabel.heightAnchor constraintEqualToConstant:30]
    ]];
    
    // 音量标签（从时间标签后面开始排列）
    NSTextField *volumeLabel = [[NSTextField alloc] init];
    volumeLabel.stringValue = @"音量";
    volumeLabel.editable = NO;
    volumeLabel.bordered = NO;
    volumeLabel.backgroundColor = [NSColor clearColor];
    volumeLabel.textColor = [NSColor whiteColor];
    [buttonContainer addSubview:volumeLabel];
    
    volumeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [volumeLabel.leadingAnchor constraintGreaterThanOrEqualToAnchor:_timeLabel.trailingAnchor constant:20],
        [volumeLabel.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [volumeLabel.widthAnchor constraintEqualToConstant:40]
    ]];
    
    // 音量滑块
    _volumeSlider = [[NSSlider alloc] init];
    _volumeSlider.minValue = 0;
    _volumeSlider.maxValue = 100;
    _volumeSlider.doubleValue = 100;
    _volumeSlider.continuous = YES;
    _volumeSlider.target = self;
    _volumeSlider.action = @selector(volumeSliderChanged:);
    [buttonContainer addSubview:_volumeSlider];
    
    _volumeSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_volumeSlider.leadingAnchor constraintEqualToAnchor:volumeLabel.trailingAnchor constant:10],
        [_volumeSlider.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_volumeSlider.widthAnchor constraintEqualToConstant:100]
    ]];
    
    // 速度标签
    NSTextField *speedLabel = [[NSTextField alloc] init];
    speedLabel.stringValue = @"速度";
    speedLabel.editable = NO;
    speedLabel.bordered = NO;
    speedLabel.backgroundColor = [NSColor clearColor];
    speedLabel.textColor = [NSColor whiteColor];
    [buttonContainer addSubview:speedLabel];
    
    speedLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [speedLabel.leadingAnchor constraintEqualToAnchor:_volumeSlider.trailingAnchor constant:10],
        [speedLabel.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [speedLabel.widthAnchor constraintEqualToConstant:40]
    ]];
    
    // 速度选择器
    _speedButton = [[NSPopUpButton alloc] init];
    [_speedButton addItemsWithTitles:@[@"0.5x", @"0.75x", @"1.0x", @"1.25x", @"1.5x", @"2.0x"]];
    [_speedButton selectItemAtIndex:2];  // 默认 1.0x
    _speedButton.target = self;
    _speedButton.action = @selector(speedButtonChanged:);
    [buttonContainer addSubview:_speedButton];
    
    _speedButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_speedButton.leadingAnchor constraintEqualToAnchor:speedLabel.trailingAnchor constant:10],
        [_speedButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_speedButton.widthAnchor constraintEqualToConstant:70]
    ]];
    
    // 显示模式按钮
    _aspectRatioButton = [[NSButton alloc] init];
    _aspectRatioButton.title = @"适应";
    _aspectRatioButton.bezelStyle = NSBezelStyleRounded;
    _aspectRatioButton.target = self;
    _aspectRatioButton.action = @selector(aspectRatioButtonClicked:);
    [buttonContainer addSubview:_aspectRatioButton];
    
    _aspectRatioButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_aspectRatioButton.leadingAnchor constraintEqualToAnchor:_speedButton.trailingAnchor constant:10],
        [_aspectRatioButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_aspectRatioButton.widthAnchor constraintEqualToConstant:60],
        [_aspectRatioButton.heightAnchor constraintEqualToConstant:30]
    ]];

    _downloadButton = [[NSButton alloc] init];
    _downloadButton.title = @"下载";
    _downloadButton.bezelStyle = NSBezelStyleRounded;
    _downloadButton.target = self;
    _downloadButton.action = @selector(downloadButtonClicked:);
    [buttonContainer addSubview:_downloadButton];

    _downloadButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_downloadButton.leadingAnchor constraintEqualToAnchor:_aspectRatioButton.trailingAnchor constant:10],
        [_downloadButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_downloadButton.widthAnchor constraintEqualToConstant:72],
        [_downloadButton.heightAnchor constraintEqualToConstant:30]
    ]];

    _completedDownloadsButton = [[NSButton alloc] init];
    _completedDownloadsButton.title = @"已下载";
    _completedDownloadsButton.bezelStyle = NSBezelStyleRounded;
    _completedDownloadsButton.target = self;
    _completedDownloadsButton.action = @selector(completedDownloadsButtonClicked:);
    _completedDownloadsButton.toolTip = @"展开已完成列表，双击条目播放";
    [buttonContainer addSubview:_completedDownloadsButton];

    _completedDownloadsButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_completedDownloadsButton.leadingAnchor constraintEqualToAnchor:_downloadButton.trailingAnchor constant:10],
        [_completedDownloadsButton.trailingAnchor constraintLessThanOrEqualToAnchor:buttonContainer.trailingAnchor constant:-10],
        [_completedDownloadsButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_completedDownloadsButton.widthAnchor constraintEqualToConstant:72],
        [_completedDownloadsButton.heightAnchor constraintEqualToConstant:30]
    ]];
}

#pragma mark - Actions

- (void)openButtonClicked:(id)sender {
    // 创建选择对话框
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"打开视频";
    alert.informativeText = @"请选择视频来源";
    [alert addButtonWithTitle:@"本地文件"];
    [alert addButtonWithTitle:@"网络地址"];
    [alert addButtonWithTitle:@"取消"];
    
    NSModalResponse response = [alert runModal];
    
    if (response == NSAlertFirstButtonReturn) {
        // 本地文件
        [self openLocalFile];
    } else if (response == NSAlertSecondButtonReturn) {
        // 网络地址
        [self openNetworkURL];
    }
}

- (void)openLocalFile {
    NSOpenPanel *openPanel = [NSOpenPanel openPanel];
    openPanel.allowedFileTypes = @[@"mp4", @"mkv", @"avi", @"mov", @"flv", @"wmv", @"m4v", @"3gp", @"ts", @"m3u8", @"bin"];
    openPanel.canChooseFiles = YES;
    openPanel.canChooseDirectories = NO;
    openPanel.allowsMultipleSelection = NO;
    openPanel.message = @"选择要播放的视频文件";
    
    [openPanel beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            NSURL *fileURL = openPanel.URLs.firstObject;
            [self openURL:fileURL.path];
        }
    }];
}

- (void)openNetworkURL {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"输入网络地址";
    alert.informativeText = @"请输入视频的网络地址（HTTP/HTTPS）";
    [alert addButtonWithTitle:@"确定"];
    [alert addButtonWithTitle:@"取消"];
    
    // 创建输入框
    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 400, 24)];
    input.placeholderString = @"https://example.com/video.mp4";
//    input.stringValue = @"https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4";
    input.stringValue = @"https://vod-volcengine.cskziwl.cn/P6N8MWsjc58A5Rb3/K7XpsqzzPY1dGv5f.mp4";
//    input.stringValue = @"rtmp://liveplay.shanhuketang.com/live/2619_tenghuiwangxiao_live_560021_1";
//    input.stringValue = @"https://v.shkt.online/772388bdvodtranscq1317978474/4ece4b555145403697569546683/v.f1440843.mp4";
//    input.stringValue = @"https://vod.tenwiseacademy.cn/111453136245362688/0e19tzp2z8r2y8qqrhec87qqougy9hcg/hhAFpacIYZ4A.mp4";//h265
    alert.accessoryView = input;
    
    // 设置输入框为第一响应者
    [alert layout];
    [[alert window] makeFirstResponder:input];
    
    NSModalResponse response = [alert runModal];
    
    if (response == NSAlertFirstButtonReturn) {
        NSString *urlString = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (urlString.length > 0) {
            [self openURL:urlString];
        } else {
            [self showErrorAlert:@"请输入有效的网络地址"];
        }
    }
}

- (void)showErrorAlert:(NSString *)message {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"错误";
    alert.informativeText = message;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"确定"];
    [alert runModal];
}

- (void)playPauseButtonClicked:(id)sender {
    if (_player.state == HXCPlayerStatePlaying) {
        [_player pause];
    } else {
        [_player play];
    }
}

- (void)stopButtonClicked:(id)sender {
    [_player stop];
}

- (void)aspectRatioButtonClicked:(id)sender {
    HXCAspectRatioMode currentMode = _player.aspectRatioMode;
    if (currentMode == HXCAspectRatioModeFit) {
        _player.aspectRatioMode = HXCAspectRatioModeFill;
        _aspectRatioButton.title = @"填充";
    } else {
        _player.aspectRatioMode = HXCAspectRatioModeFit;
        _aspectRatioButton.title = @"适应";
    }
}

- (void)downloadButtonClicked:(id)sender {
    (void)sender;
    if (!_downloadWindowController) {
        _downloadWindowController = [[HXCVDownloadWindowController alloc] init];
    }
    [_downloadWindowController showDownloadPanel];
}

- (void)completedDownloadsButtonClicked:(id)sender {
    (void)sender;
    [self hxdvd_ensureCompletedDownloadsPopover];
    self.completedDownloadsItems = [[HXCVDownloadManager sharedManager] tasksWithState:HXCVDownloadStateCompleted];
    [self.completedDownloadsTableView reloadData];
    if (self.completedDownloadsPopover.shown) {
        [self.completedDownloadsPopover performClose:nil];
        return;
    }
    if (self.completedDownloadsItems.count == 0) {
        [self showErrorAlert:@"暂无已完成的下载"];
        return;
    }
    [self.completedDownloadsPopover showRelativeToRect:self.completedDownloadsButton.bounds
                                                ofView:self.completedDownloadsButton
                                         preferredEdge:NSRectEdgeMinY];
}

- (void)hxdvd_ensureCompletedDownloadsPopover {
    if (self.completedDownloadsPopover) {
        return;
    }
    NSPopover *pop = [[NSPopover alloc] init];
    pop.behavior = NSPopoverBehaviorTransient;
    pop.animates = YES;
    pop.contentSize = NSMakeSize(440, 300);

    NSViewController *vc = [[NSViewController alloc] init];
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 440, 300)];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:container.bounds];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSBezelBorder;

    NSTableView *tv = [[NSTableView alloc] initWithFrame:scroll.bounds];
    tv.headerView = nil;
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"title"];
    col.width = 400;
    col.minWidth = 200;
    col.title = @"";
    [tv addTableColumn:col];
    tv.dataSource = self;
    tv.delegate = self;
    tv.target = self;
    tv.doubleAction = @selector(completedDownloadsTableDoubleAction:);
    scroll.documentView = tv;
    [container addSubview:scroll];
    self.completedDownloadsTableView = tv;

    vc.view = container;
    pop.contentViewController = vc;
    self.completedDownloadsPopover = pop;
}

- (void)completedDownloadsTableDoubleAction:(id)sender {
    NSTableView *tv = (NSTableView *)sender;
    NSInteger row = tv.clickedRow;
    if (row < 0) {
        row = tv.selectedRow;
    }
    if (row < 0 || row >= (NSInteger)self.completedDownloadsItems.count) {
        return;
    }
    [self hxdvd_playCompletedItem:self.completedDownloadsItems[(NSUInteger)row]];
}

- (NSString *)hxdvd_displayTitleForDownloadItem:(HXCVDownloadItem *)item {
    NSURL *nu = [NSURL URLWithString:item.urlString];
    NSString *name = nu.lastPathComponent ?: @"";
    if ([name containsString:@"?"]) {
        name = [[name componentsSeparatedByString:@"?"] firstObject];
    }
    if (name.length == 0) {
        name = @"视频";
    }
    if (item.downloadType == HXCVDownloadTypeHLS) {
        return [NSString stringWithFormat:@"%@（HLS）", name];
    }
    return name;
}

- (void)hxdvd_playCompletedItem:(HXCVDownloadItem *)item {
    NSURL *fileURL = [[HXCVDownloadManager sharedManager] playableFileURLForCompletedItem:item];
    if (!fileURL) {
        [self showErrorAlert:@"本地文件不存在或路径无效，请确认下载目录未改动"];
        return;
    }
    if (self.completedDownloadsPopover.shown) {
        [self.completedDownloadsPopover performClose:nil];
    }
    [self openURL:fileURL.path];
}

#pragma mark - NSTableViewDataSource & NSTableViewDelegate

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return (NSInteger)self.completedDownloadsItems.count;
}

- (nullable id)tableView:(NSTableView *)tableView objectValueForTableColumn:(nullable NSTableColumn *)tableColumn row:(NSInteger)row {
    (void)tableView;
    (void)tableColumn;
    if (row < 0 || row >= (NSInteger)self.completedDownloadsItems.count) {
        return @"";
    }
    return [self hxdvd_displayTitleForDownloadItem:self.completedDownloadsItems[(NSUInteger)row]];
}

#pragma mark - SeekSliderDelegate

- (void)seekSliderDidBeginTracking:(SeekSlider *)slider {
    // 用户开始拖动，暂停 UI 自动更新
    _isSeeking = YES;
}

- (void)seekSliderDidContinueTracking:(SeekSlider *)slider {
    // 拖动过程中只更新时间显示，不执行真正的 seek
    double value = slider.doubleValue;
    double duration = _player.duration;
    if (duration > 0) {
        double position = (value / 1000.0) * duration;
        _timeLabel.stringValue = [NSString stringWithFormat:@"%@ / %@",
                                  [self formatTime:position],
                                  [self formatTime:duration]];
    }
}

- (void)seekSliderDidEndTracking:(SeekSlider *)slider {
    // 用户松手，执行真正的 seek 操作
    double value = slider.doubleValue;
    double duration = _player.duration;
    if (duration > 0) {
        double position = (value / 1000.0) * duration;
        [_player seekToPosition:position];
    }
    
    // 恢复 UI 自动更新
    _isSeeking = NO;
}

- (void)volumeSliderChanged:(id)sender {
    _player.volume = _volumeSlider.doubleValue / 100.0;
}

- (void)speedButtonChanged:(id)sender {
    NSInteger index = _speedButton.indexOfSelectedItem;
    double rate = 1.0;
    switch (index) {
        case 0: rate = 0.5; break;
        case 1: rate = 0.75; break;
        case 2: rate = 1.0; break;
        case 3: rate = 1.25; break;
        case 4: rate = 1.5; break;
        case 5: rate = 2.0; break;
    }
    _player.playbackRate = rate;
    NSLog(@"设置播放速率: %.2fx", rate);
}

#pragma mark - Player Control

- (void)openURL:(NSString *)urlString {
    [_player stop];
    BOOL success = NO;
#if 1
    HXCPlayerDataSourceMode mode = HXCPlayerDataSourceModeDefault;  // 或者 HXCPlayerDataSourceModeDefault
    BOOL encryptedFile = NO;
    // 其它配置只需播放前设置一次（通过 configureDefaultConfig 缓存）。
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        HXCPlayerDataSourceConfig *base = [HXCPlayerDataSourceConfig defaultConfig];
        base.timeoutMs = 30000;           // 30秒超时
        base.maxRetries = 3;              // 最多重试3次
        base.cacheSize = 2 * 1024 * 1024; // 2MB 缓存
        base.avioBufferSize = 64 * 1024;  // 64KB AVIO 缓冲区
        [HXCPlayerDataSourceConfig configureDefaultConfig:base];
    });
    HXCPlayerDataSourcePlayModel *model = [HXCPlayerDataSourcePlayModel modelWithURL:urlString
                                                                                 mode:mode
                                                                          encryptedFile:encryptedFile];
    success = [_player playWithModel:model];
#else
    success = [_player prepareToPlay:urlString];
#endif
    
    if (success) {
    } else {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"打开失败";
        alert.informativeText = @"无法打开媒体文件";
        alert.alertStyle = NSAlertStyleWarning;
        [alert runModal];
    }
}

#pragma mark - HXCPlayerControlDelegate

// 状态变化
- (void)player:(HXCPlayerControl *)player didChangeState:(HXCPlayerState)state {
    dispatch_async(dispatch_get_main_queue(), ^{
        switch (state) {
            case HXCPlayerStatePlaying:
                self.playPauseButton.title = @"暂停";
                self.playPauseButton.enabled = YES;
                self.stopButton.enabled = YES;
                self.progressSlider.enabled = YES;
                break;
                
            case HXCPlayerStatePaused:
            case HXCPlayerStateReady:
                self.playPauseButton.title = @"播放";
                self.playPauseButton.enabled = YES;
                self.stopButton.enabled = YES;
                break;
                
            case HXCPlayerStateStopped:
            case HXCPlayerStateError:
                self.playPauseButton.title = @"播放";
                self.playPauseButton.enabled = NO;
                self.stopButton.enabled = NO;
                self.progressSlider.enabled = NO;
                self.progressSlider.doubleValue = 0;
                self.timeLabel.stringValue = @"00:00 / 00:00";
                break;
                
            default:
                break;
        }
    });
}

// 错误通知
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"播放错误";
        alert.informativeText = error.localizedDescription;
        alert.alertStyle = NSAlertStyleCritical;
        [alert runModal];
    });
}

// 播放进度更新（真实播放位置）
- (void)player:(HXCPlayerControl *)player didUpdatePosition:(double)position {
    // 如果正在 seek，跳过 UI 更新避免冲突
    if (_isSeeking) {
        return;
    }
    
    double duration = player.duration;
    
    // 只要有有效的时长，就更新进度显示
    if (duration > 0) {
        double value = (position / duration) * 1000.0;
        self.progressSlider.doubleValue = value;
        
        self.timeLabel.stringValue = [NSString stringWithFormat:@"%@ / %@",
                                      [self formatTime:position],
                                      [self formatTime:duration]];
    }
}

// 缓冲进度更新（解码位置）
- (void)player:(HXCPlayerControl *)player didUpdateBufferProgress:(double)position {
    // 可以在这里显示缓冲进度
    NSLog(@"缓冲进度: %.2f", position);
}

-(void)playerDidFinishPlaying:(HXCPlayerControl *)player {
    NSLog(@"视频播放结束...");
}

#pragma mark - Helper Methods

- (NSString *)formatTime:(double)seconds {
    int totalSeconds = (int)seconds;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;
    
    if (hours > 0) {
        return [NSString stringWithFormat:@"%d:%02d:%02d", hours, minutes, secs];
    } else {
        return [NSString stringWithFormat:@"%02d:%02d", minutes, secs];
    }
}

@end
