//
//  ViewController.m
//  HXCPlayerMacOSTest
//
//  Created by HXCPlayer Team
//  演示如何使用 HXCPlayer.xcframework
//

#import "ViewController.h"
#import <HXCPlayer/HXCPlayer.h>
#import "SeekSlider.h"

@interface ViewController () <HXCPlayerControlDelegate, SeekSliderDelegate>

@property (nonatomic, strong) HXCPlayerControl *player;
@property (nonatomic, strong) NSView *playerView;
@property (nonatomic, strong) NSButton *openButton;
@property (nonatomic, strong) NSButton *playPauseButton;
@property (nonatomic, strong) NSButton *stopButton;
@property (nonatomic, strong) NSButton *aspectRatioButton;
@property (nonatomic, strong) SeekSlider *progressSlider;
@property (nonatomic, strong) NSSlider *volumeSlider;
@property (nonatomic, strong) NSPopUpButton *speedButton;
@property (nonatomic, strong) NSTextField *timeLabel;
@property (nonatomic, strong) NSTextField *titleLabel;
@property (nonatomic, assign) BOOL isPlaying;
@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.isPlaying = NO;
    self.isSeeking = NO;
    
    [self setupPlayer];
    [self setupUI];
}

- (void)setupPlayer {
    // 创建播放器控制器
    self.player = [[HXCPlayerControl alloc] init];
//    self.player.startPosition = 57;
    self.player.delegate = self;
    
    // 获取视频视图
    NSView *videoView = [self.player videoView];
    videoView.wantsLayer = YES;
    videoView.layer.backgroundColor = [[NSColor blackColor] CGColor];
    videoView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:videoView];
    self.playerView = videoView;
    
    // 视频视图约束 - 在标题栏和控制栏之间
    [NSLayoutConstraint activateConstraints:@[
        [videoView.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:50],
        [videoView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [videoView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [videoView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-110]
    ]];
    
    // 默认打开测试视频
    NSString *testURL = @"https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4";
//    NSString *testURL = @"http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";
    [self.player playURL:testURL];
}

- (void)setupUI {
    // ========== 顶部标题栏 ==========
    NSBox *titleBar = [[NSBox alloc] init];
    titleBar.boxType = NSBoxCustom;
    titleBar.fillColor = [NSColor colorWithWhite:0.15 alpha:1.0];
    titleBar.borderColor = [NSColor clearColor];
    [self.view addSubview:titleBar];
    
    titleBar.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [titleBar.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [titleBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [titleBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [titleBar.heightAnchor constraintEqualToConstant:50]
    ]];
    
    // 视频标题标签
    self.titleLabel = [[NSTextField alloc] init];
    self.titleLabel.stringValue = @"HXC Player - macOS 测试";
    self.titleLabel.editable = NO;
    self.titleLabel.bordered = NO;
    self.titleLabel.backgroundColor = [NSColor clearColor];
    self.titleLabel.textColor = [NSColor whiteColor];
    self.titleLabel.font = [NSFont boldSystemFontOfSize:16];
    self.titleLabel.alignment = NSTextAlignmentCenter;
    [titleBar addSubview:self.titleLabel];
    
    self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.titleLabel.centerXAnchor constraintEqualToAnchor:titleBar.centerXAnchor],
        [self.titleLabel.centerYAnchor constraintEqualToAnchor:titleBar.centerYAnchor],
        [self.titleLabel.widthAnchor constraintGreaterThanOrEqualToConstant:200]
    ]];
    
    // ========== 底部控制栏 ==========
    NSBox *controlBar = [[NSBox alloc] init];
    controlBar.boxType = NSBoxCustom;
    controlBar.fillColor = [NSColor colorWithWhite:0.15 alpha:1.0];
    controlBar.borderColor = [NSColor clearColor];
    [self.view addSubview:controlBar];
    
    controlBar.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [controlBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [controlBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [controlBar.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [controlBar.heightAnchor constraintEqualToConstant:110]
    ]];
    
    // ========== 进度条（靠顶部）==========
    self.progressSlider = [[SeekSlider alloc] init];
    self.progressSlider.minValue = 0;
    self.progressSlider.maxValue = 1;
    self.progressSlider.doubleValue = 0;
    self.progressSlider.continuous = YES;
    self.progressSlider.seekDelegate = self;
    [controlBar addSubview:self.progressSlider];
    self.progressSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.progressSlider.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:10],
        [self.progressSlider.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:80],
        [self.progressSlider.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-80],
        [self.progressSlider.heightAnchor constraintEqualToConstant:20]
    ]];
    
    // 时间标签（进度条下方居中）
    self.timeLabel = [[NSTextField alloc] init];
    self.timeLabel.stringValue = @"00:00 / 00:00";
    self.timeLabel.editable = NO;
    self.timeLabel.bordered = NO;
    self.timeLabel.backgroundColor = [NSColor clearColor];
    self.timeLabel.textColor = [NSColor whiteColor];
    self.timeLabel.alignment = NSTextAlignmentCenter;
    self.timeLabel.font = [NSFont systemFontOfSize:12];
    [controlBar addSubview:self.timeLabel];
    
    self.timeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.timeLabel.topAnchor constraintEqualToAnchor:self.progressSlider.bottomAnchor constant:2],
        [self.timeLabel.centerXAnchor constraintEqualToAnchor:controlBar.centerXAnchor],
        [self.timeLabel.widthAnchor constraintEqualToConstant:120]
    ]];
    
    // ========== 底部按钮行 ==========
    
    // 左侧：播放/暂停、停止按钮
    self.playPauseButton = [[NSButton alloc] init];
    self.playPauseButton.title = @"播放";
    self.playPauseButton.bezelStyle = NSBezelStyleRounded;
    self.playPauseButton.target = self;
    self.playPauseButton.action = @selector(togglePlayPause:);
    [controlBar addSubview:self.playPauseButton];
    
    self.playPauseButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.playPauseButton.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:20],
        [self.playPauseButton.bottomAnchor constraintEqualToAnchor:controlBar.bottomAnchor constant:-20],
        [self.playPauseButton.widthAnchor constraintEqualToConstant:70],
        [self.playPauseButton.heightAnchor constraintEqualToConstant:32]
    ]];
    
    self.stopButton = [[NSButton alloc] init];
    self.stopButton.title = @"停止";
    self.stopButton.bezelStyle = NSBezelStyleRounded;
    self.stopButton.target = self;
    self.stopButton.action = @selector(stopVideo:);
    [controlBar addSubview:self.stopButton];
    
    self.stopButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.stopButton.leadingAnchor constraintEqualToAnchor:self.playPauseButton.trailingAnchor constant:10],
        [self.stopButton.bottomAnchor constraintEqualToAnchor:controlBar.bottomAnchor constant:-20],
        [self.stopButton.widthAnchor constraintEqualToConstant:70],
        [self.stopButton.heightAnchor constraintEqualToConstant:32]
    ]];
    
    // 右侧：音量、速度、适应按钮（从右向左布局）
    
    // 适应按钮（最右侧）
    self.aspectRatioButton = [[NSButton alloc] init];
    self.aspectRatioButton.title = @"适应";
    self.aspectRatioButton.bezelStyle = NSBezelStyleRounded;
    self.aspectRatioButton.target = self;
    self.aspectRatioButton.action = @selector(toggleAspectRatio:);
    [controlBar addSubview:self.aspectRatioButton];
    
    self.aspectRatioButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.aspectRatioButton.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-20],
        [self.aspectRatioButton.bottomAnchor constraintEqualToAnchor:controlBar.bottomAnchor constant:-20],
        [self.aspectRatioButton.widthAnchor constraintEqualToConstant:70],
        [self.aspectRatioButton.heightAnchor constraintEqualToConstant:32]
    ]];
    
    // 速度选择器
    self.speedButton = [[NSPopUpButton alloc] init];
    [self.speedButton addItemsWithTitles:@[@"0.5x", @"0.75x", @"1.0x", @"1.25x", @"1.5x", @"2.0x"]];
    [self.speedButton selectItemAtIndex:2];  // 默认 1.0x
    self.speedButton.target = self;
    self.speedButton.action = @selector(speedButtonChanged:);
    [controlBar addSubview:self.speedButton];
    
    self.speedButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.speedButton.trailingAnchor constraintEqualToAnchor:self.aspectRatioButton.leadingAnchor constant:-10],
        [self.speedButton.bottomAnchor constraintEqualToAnchor:controlBar.bottomAnchor constant:-20],
        [self.speedButton.widthAnchor constraintEqualToConstant:80],
        [self.speedButton.heightAnchor constraintEqualToConstant:32]
    ]];
    
    NSTextField *speedLabel = [[NSTextField alloc] init];
    speedLabel.stringValue = @"速度";
    speedLabel.editable = NO;
    speedLabel.bordered = NO;
    speedLabel.backgroundColor = [NSColor clearColor];
    speedLabel.textColor = [NSColor lightGrayColor];
    speedLabel.font = [NSFont systemFontOfSize:12];
    [controlBar addSubview:speedLabel];
    
    speedLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [speedLabel.trailingAnchor constraintEqualToAnchor:self.speedButton.leadingAnchor constant:-5],
        [speedLabel.centerYAnchor constraintEqualToAnchor:self.speedButton.centerYAnchor],
        [speedLabel.widthAnchor constraintEqualToConstant:40]
    ]];
    
    // 音量滑块
    self.volumeSlider = [[NSSlider alloc] init];
    self.volumeSlider.minValue = 0;
    self.volumeSlider.maxValue = 1;
    self.volumeSlider.doubleValue = 1.0;
    self.volumeSlider.continuous = YES;
    self.volumeSlider.target = self;
    self.volumeSlider.action = @selector(volumeSliderChanged:);
    [controlBar addSubview:self.volumeSlider];
    
    self.volumeSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.volumeSlider.trailingAnchor constraintEqualToAnchor:speedLabel.leadingAnchor constant:-10],
        [self.volumeSlider.centerYAnchor constraintEqualToAnchor:self.speedButton.centerYAnchor],
        [self.volumeSlider.widthAnchor constraintEqualToConstant:100],
        [self.volumeSlider.heightAnchor constraintEqualToConstant:20]
    ]];
    
    NSTextField *volumeLabel = [[NSTextField alloc] init];
    volumeLabel.stringValue = @"音量";
    volumeLabel.editable = NO;
    volumeLabel.bordered = NO;
    volumeLabel.backgroundColor = [NSColor clearColor];
    volumeLabel.textColor = [NSColor lightGrayColor];
    volumeLabel.font = [NSFont systemFontOfSize:12];
    [controlBar addSubview:volumeLabel];
    
    volumeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [volumeLabel.trailingAnchor constraintEqualToAnchor:self.volumeSlider.leadingAnchor constant:-5],
        [volumeLabel.centerYAnchor constraintEqualToAnchor:self.volumeSlider.centerYAnchor],
        [volumeLabel.widthAnchor constraintEqualToConstant:40]
    ]];
}

#pragma mark - 控制按钮操作

- (void)togglePlayPause:(id)sender {
    if (self.isPlaying) {
        [self.player pause];
        self.playPauseButton.title = @"播放";
        self.titleLabel.stringValue = @"⏸ 已暂停";
        self.isPlaying = NO;
    } else {
        [self.player play];
        self.playPauseButton.title = @"暂停";
        self.titleLabel.stringValue = @"▶️ 播放中...";
        self.isPlaying = YES;
    }
}

- (void)stopVideo:(id)sender {
    [self.player stop];
    self.playPauseButton.title = @"播放";
    self.titleLabel.stringValue = @"⏹ 已停止";
    self.isPlaying = NO;
}

- (void)toggleAspectRatio:(id)sender {
    static HXCAspectRatioMode modes[] = {HXCAspectRatioModeFit, HXCAspectRatioModeFill};
    static NSString *modeNames[] = {@"适应", @"填充"};
    static NSInteger currentIndex = 0;
    
    currentIndex = (currentIndex + 1) % 2;
    
    self.player.aspectRatioMode = modes[currentIndex];
    self.aspectRatioButton.title = modeNames[currentIndex];
    self.titleLabel.stringValue = [NSString stringWithFormat:@"📐 视频比例: %@", modeNames[currentIndex]];
}

- (void)speedButtonChanged:(id)sender {
    NSInteger index = self.speedButton.indexOfSelectedItem;
    double speed = 1.0;
    switch (index) {
        case 0: speed = 0.5; break;
        case 1: speed = 0.75; break;
        case 2: speed = 1.0; break;
        case 3: speed = 1.25; break;
        case 4: speed = 1.5; break;
        case 5: speed = 2.0; break;
    }
    
    [self.player setPlaybackRate:speed];
    self.titleLabel.stringValue = [NSString stringWithFormat:@"⚡️ %.2f倍速播放", speed];
}

#pragma mark - SeekSliderDelegate

- (void)seekSliderDidBeginTracking:(SeekSlider *)slider {
    // 用户开始拖动，暂停 UI 自动更新
    self.isSeeking = YES;
}

- (void)seekSliderDidContinueTracking:(SeekSlider *)slider {
    // 拖动过程中只更新时间显示，不执行真正的 seek
    if (self.player.duration > 0) {
        double position = slider.doubleValue * self.player.duration;
        self.timeLabel.stringValue = [NSString stringWithFormat:@"%@ / %@",
                                      [self formatTime:position],
                                      [self formatTime:self.player.duration]];
    }
}

- (void)seekSliderDidEndTracking:(SeekSlider *)slider {
    // 用户松手，执行真正的 seek 操作
    if (self.player.duration > 0) {
        double position = slider.doubleValue * self.player.duration;
        [self.player seekToPosition:position];
        self.titleLabel.stringValue = [NSString stringWithFormat:@"⏩ 跳转到 %@", [self formatTime:position]];
    }
    
    // 恢复 UI 自动更新
    self.isSeeking = NO;
}

#pragma mark - 滑块操作

- (void)volumeSliderChanged:(NSSlider *)slider {
    self.player.volume = slider.doubleValue;
}

#pragma mark - HXCPlayerControlDelegate

- (void)playerDidChangeState:(HXCPlayerState)state {
    NSString *stateText = @"";
    switch (state) {
        case HXCPlayerStateIdle:
            stateText = @"空闲";
            break;
        case HXCPlayerStateOpening:
            stateText = @"正在打开...";
            break;
        case HXCPlayerStateLoading:
            stateText = @"⏳ 加载中...";
            break;
        case HXCPlayerStatePlaying:
            stateText = @"▶️ 播放中";
            self.isPlaying = YES;
            self.playPauseButton.title = @"暂停";
            break;
        case HXCPlayerStatePaused:
            stateText = @"⏸ 已暂停";
            self.isPlaying = NO;
            self.playPauseButton.title = @"播放";
            break;
        case HXCPlayerStateStopped:
            stateText = @"⏹ 已停止";
            self.isPlaying = NO;
            self.playPauseButton.title = @"播放";
            break;
        case HXCPlayerStateError:
            stateText = @"❌ 错误";
            self.isPlaying = NO;
            self.playPauseButton.title = @"播放";
            break;
    }
    
    self.titleLabel.stringValue = stateText;
}

- (void)playerDidEncounterError:(NSError *)error {
    self.titleLabel.stringValue = [NSString stringWithFormat:@"❌ 错误: %@", error.localizedDescription];
}

- (void)playerDidUpdatePosition:(double)position duration:(double)duration {
    // 拖动进度条时跳过更新，避免冲突
    if (self.isSeeking) {
        return;
    }
    
    // 更新进度条和时间标签
    if (duration > 0) {
        self.progressSlider.doubleValue = position / duration;
        
        self.timeLabel.stringValue = [NSString stringWithFormat:@"%@ / %@",
                                      [self formatTime:position],
                                      [self formatTime:duration]];
    }
}

#pragma mark - 辅助方法

- (NSString *)formatTime:(double)seconds {
    int hours = (int)seconds / 3600;
    int minutes = ((int)seconds % 3600) / 60;
    int secs = (int)seconds % 60;
    
    if (hours > 0) {
        return [NSString stringWithFormat:@"%02d:%02d:%02d", hours, minutes, secs];
    } else {
        return [NSString stringWithFormat:@"%02d:%02d", minutes, secs];
    }
}

- (void)dealloc {
    [self.player stop];
}

@end
