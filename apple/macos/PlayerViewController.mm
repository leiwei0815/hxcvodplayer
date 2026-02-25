/**
 * @file PlayerViewController.mm
 * @brief macOS 播放器视图控制器实现
 */

#import "PlayerViewController.h"
#import "../HXCPlayerControl.h"  // 使用统一的播放器类
#import "HXCPlayerView.h"

@interface PlayerViewController () <HXCPlayerControlDelegate>

@property (nonatomic, strong) HXCPlayerControl *player;
@property (nonatomic, strong) HXCPlayerView *playerView;

// UI 控件
@property (nonatomic, strong) NSButton *openButton;
@property (nonatomic, strong) NSButton *playPauseButton;
@property (nonatomic, strong) NSButton *stopButton;
@property (nonatomic, strong) NSButton *aspectRatioButton;
@property (nonatomic, strong) NSSlider *progressSlider;
@property (nonatomic, strong) NSSlider *volumeSlider;
@property (nonatomic, strong) NSPopUpButton *speedButton;
@property (nonatomic, strong) NSTextField *timeLabel;
@property (nonatomic, strong) NSTimer *updateTimer;

@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation PlayerViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 720)];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    
    [self setupPlayer];
    [self setupUI];
    [self startUpdateTimer];
}

- (void)dealloc {
    [_updateTimer invalidate];
    [_player close];
}

#pragma mark - Setup

- (void)setupPlayer {
    // 创建播放器
    _player = [[HXCPlayerControl alloc] init];
    _player.delegate = self;
    
    // 创建播放器视图
    NSRect playerFrame = NSMakeRect(0, 100, 1280, 620);
    _playerView = [[HXCPlayerView alloc] initWithFrame:playerFrame];
    [_playerView setPlayer:_player];
    [self.view addSubview:_playerView];
    
    // 设置自动布局
    _playerView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_playerView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_playerView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_playerView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_playerView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-100]
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
    _progressSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(20, 70, 1240, 20)];
    _progressSlider.minValue = 0;
    _progressSlider.maxValue = 1000;
    _progressSlider.continuous = YES;
    _progressSlider.target = self;
    _progressSlider.action = @selector(progressSliderChanged:);
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
        [_aspectRatioButton.trailingAnchor constraintLessThanOrEqualToAnchor:buttonContainer.trailingAnchor constant:-10],
        [_aspectRatioButton.centerYAnchor constraintEqualToAnchor:buttonContainer.centerYAnchor],
        [_aspectRatioButton.widthAnchor constraintEqualToConstant:60],
        [_aspectRatioButton.heightAnchor constraintEqualToConstant:30]
    ]];
}

- (void)startUpdateTimer {
    _updateTimer = [NSTimer scheduledTimerWithTimeInterval:0.1
                                                    target:self
                                                  selector:@selector(updateUI)
                                                  userInfo:nil
                                                   repeats:YES];
}

#pragma mark - Actions

- (void)openButtonClicked:(id)sender {
//    NSOpenPanel *openPanel = [NSOpenPanel openPanel];
//    openPanel.allowedFileTypes = @[@"mp4", @"mkv", @"avi", @"mov", @"flv", @"wmv"];
//    openPanel.canChooseFiles = YES;
//    openPanel.canChooseDirectories = NO;
//    openPanel.allowsMultipleSelection = NO;
//    
//    [openPanel beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse result) {
//        if (result == NSModalResponseOK) {
//            NSURL *fileURL = openPanel.URLs.firstObject;
//            [self openURL:fileURL.path];
//        }
//    }];
    
    // 或者直接打开一个测试 URL
     [self openURL:@"https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4"];
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

- (void)progressSliderChanged:(id)sender {
    if (!_isSeeking) {
        _isSeeking = YES;
        return;
    }
    
    double value = _progressSlider.doubleValue;
    double duration = _player.duration;
    if (duration > 0) {
        double position = (value / 1000.0) * duration;
        [_player seekToPosition:position];
    }
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
    [_player close];
    
    if ([_player prepareToPlay:urlString]) {
        [_player play];
    } else {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"打开失败";
        alert.informativeText = @"无法打开媒体文件";
        alert.alertStyle = NSAlertStyleWarning;
        [alert runModal];
    }
}

- (void)updateUI {
    // 如果正在 seek，跳过 UI 更新避免冲突
    if (_isSeeking) {
        return;
    }
    
    double position = _player.position;
    double duration = _player.duration;
    
    // 只要有有效的时长，就更新进度显示（不管是播放还是暂停状态）
    if (duration > 0) {
        double value = (position / duration) * 1000.0;
        _progressSlider.doubleValue = value;
        
        _timeLabel.stringValue = [NSString stringWithFormat:@"%@ / %@",
                                  [self formatTime:position],
                                  [self formatTime:duration]];
    }
}

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

#pragma mark - HXCPlayerControlDelegate

- (void)playerDidChangeState:(HXCPlayerState)state {
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

- (void)playerDidEncounterError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"播放错误";
        alert.informativeText = error.localizedDescription;
        alert.alertStyle = NSAlertStyleCritical;
        [alert runModal];
    });
}

@end
