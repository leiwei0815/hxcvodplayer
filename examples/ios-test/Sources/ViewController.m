//
//  ViewController.m
//  HXCPlayerIOSTest
//
//  Created by HXCPlayer Team
//  演示如何使用 HXCPlayer.xcframework
//

#import "ViewController.h"
#import <HXCPlayer/HXCPlayer.h>

@interface ViewController () <HXCPlayerControlDelegate>

@property (nonatomic, strong) HXCPlayerControl *player;
@property (nonatomic, strong) UIButton *playPauseButton;
@property (nonatomic, strong) UIButton *speedButton;
@property (nonatomic, strong) UIButton *aspectRatioButton;
@property (nonatomic, strong) UISlider *progressSlider;
@property (nonatomic, strong) UISlider *volumeSlider;
@property (nonatomic, strong) UILabel *timeLabel;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *volumeLabel;
@property (nonatomic, assign) BOOL isPlaying;
@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.view.backgroundColor = [UIColor whiteColor];
    self.isPlaying = NO;
    self.isSeeking = NO;
    
    [self setupPlayer];
    [self setupUI];
}

- (void)setupPlayer {
    // 创建播放器控制器
    self.player = [[HXCPlayerControl alloc] init];
    self.player.delegate = self;
    
    // 获取视频视图
    UIView *videoView = [self.player videoView];
    videoView.frame = CGRectMake(0, 100, self.view.bounds.size.width, 250);
    videoView.backgroundColor = [UIColor blackColor];
    [self.view addSubview:videoView];
    
    // 测试视频 URL
//    NSString *testURL = @"https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4";
    NSString *testURL = @"https://vod.tenwiseacademy.cn/111453136245362688/lf9cmlwy92fmszkjd6qaux2s7qhennhk/k43g4cz9f5c1sva3.m3u8";
    
    // 打开视频
    [self.player playURL:testURL];
}

- (void)setupUI {
    CGFloat screenWidth = self.view.bounds.size.width;
    CGFloat padding = 20;
    CGFloat controlY = 360;
    
    // 状态标签
    self.statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(padding, 50, screenWidth - padding * 2, 30)];
    self.statusLabel.text = @"HXCPlayer iOS 测试";
    self.statusLabel.textAlignment = NSTextAlignmentCenter;
    self.statusLabel.font = [UIFont boldSystemFontOfSize:18];
    [self.view addSubview:self.statusLabel];
    
    // 进度条
    self.progressSlider = [[UISlider alloc] initWithFrame:CGRectMake(padding, controlY, screenWidth - padding * 2, 30)];
    self.progressSlider.minimumValue = 0;
    self.progressSlider.maximumValue = 1;
    self.progressSlider.value = 0;
    [self.progressSlider addTarget:self action:@selector(progressSliderTouchDown:) forControlEvents:UIControlEventTouchDown];
    [self.progressSlider addTarget:self action:@selector(progressSliderValueChanged:) forControlEvents:UIControlEventValueChanged];
    [self.progressSlider addTarget:self action:@selector(progressSliderTouchUp:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside];
    [self.view addSubview:self.progressSlider];
    
    // 时间标签
    self.timeLabel = [[UILabel alloc] initWithFrame:CGRectMake(padding, controlY + 35, screenWidth - padding * 2, 20)];
    self.timeLabel.text = @"00:00 / 00:00";
    self.timeLabel.textAlignment = NSTextAlignmentCenter;
    self.timeLabel.font = [UIFont systemFontOfSize:14];
    self.timeLabel.textColor = [UIColor grayColor];
    [self.view addSubview:self.timeLabel];
    
    // 控制按钮区域
    CGFloat buttonY = controlY + 65;
    CGFloat buttonWidth = 90;
    CGFloat spacing = 15;
    CGFloat totalWidth = buttonWidth * 3 + spacing * 2;
    CGFloat startX = (screenWidth - totalWidth) / 2;
    
    // 播放/暂停按钮
    self.playPauseButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.playPauseButton.frame = CGRectMake(startX, buttonY, buttonWidth, 44);
    [self.playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
    self.playPauseButton.backgroundColor = [UIColor systemBlueColor];
    [self.playPauseButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.playPauseButton.layer.cornerRadius = 8;
    [self.playPauseButton addTarget:self action:@selector(togglePlayPause) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.playPauseButton];
    
    // 变速按钮
    self.speedButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.speedButton.frame = CGRectMake(startX + buttonWidth + spacing, buttonY, buttonWidth, 44);
    [self.speedButton setTitle:@"1.0x" forState:UIControlStateNormal];
    self.speedButton.backgroundColor = [UIColor systemOrangeColor];
    [self.speedButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.speedButton.layer.cornerRadius = 8;
    [self.speedButton addTarget:self action:@selector(toggleSpeed) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.speedButton];
    
    // 视频比例按钮
    self.aspectRatioButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.aspectRatioButton.frame = CGRectMake(startX + (buttonWidth + spacing) * 2, buttonY, buttonWidth, 44);
    [self.aspectRatioButton setTitle:@"适应" forState:UIControlStateNormal];
    self.aspectRatioButton.backgroundColor = [UIColor systemGreenColor];
    [self.aspectRatioButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.aspectRatioButton.layer.cornerRadius = 8;
    [self.aspectRatioButton addTarget:self action:@selector(toggleAspectRatio) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.aspectRatioButton];
    
    // 音量控制区域
    CGFloat volumeY = buttonY + 60;
    self.volumeLabel = [[UILabel alloc] initWithFrame:CGRectMake(padding, volumeY, 60, 30)];
    self.volumeLabel.text = @"音量:";
    self.volumeLabel.font = [UIFont systemFontOfSize:14];
    [self.view addSubview:self.volumeLabel];
    
    self.volumeSlider = [[UISlider alloc] initWithFrame:CGRectMake(padding + 60, volumeY, screenWidth - padding * 2 - 60, 30)];
    self.volumeSlider.minimumValue = 0;
    self.volumeSlider.maximumValue = 1;
    self.volumeSlider.value = 1.0;
    [self.volumeSlider addTarget:self action:@selector(volumeSliderChanged:) forControlEvents:UIControlEventValueChanged];
    [self.view addSubview:self.volumeSlider];
    
    // 功能说明
    UILabel *infoLabel = [[UILabel alloc] initWithFrame:CGRectMake(padding, volumeY + 45, screenWidth - padding * 2, 80)];
    infoLabel.text = @"✅ 支持播放进度拖拽\n✅ 支持变速播放（0.5x-2.0x）\n✅ 支持音量调节\n✅ 支持视频比例切换";
    infoLabel.numberOfLines = 0;
    infoLabel.textAlignment = NSTextAlignmentCenter;
    infoLabel.font = [UIFont systemFontOfSize:13];
    infoLabel.textColor = [UIColor grayColor];
    [self.view addSubview:infoLabel];
}

#pragma mark - 控制按钮操作

- (void)togglePlayPause {
    if (self.isPlaying) {
        [self.player pause];
        [self.playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
        self.statusLabel.text = @"⏸ 已暂停";
        self.isPlaying = NO;
    } else {
        [self.player play];
        [self.playPauseButton setTitle:@"暂停" forState:UIControlStateNormal];
        self.statusLabel.text = @"▶️ 播放中...";
        self.isPlaying = YES;
    }
}

- (void)toggleSpeed {
    static NSArray *speeds = nil;
    static NSInteger currentIndex = 2; // 默认 1.0x
    
    if (!speeds) {
        speeds = @[@0.5, @0.75, @1.0, @1.25, @1.5, @2.0];
    }
    
    currentIndex = (currentIndex + 1) % speeds.count;
    double speed = [speeds[currentIndex] doubleValue];
    
    [self.player setPlaybackRate:speed];
    [self.speedButton setTitle:[NSString stringWithFormat:@"%.2fx", speed] forState:UIControlStateNormal];
    self.statusLabel.text = [NSString stringWithFormat:@"⚡️ %.2f倍速播放", speed];
}

- (void)toggleAspectRatio {
    static HXCAspectRatioMode modes[] = {HXCAspectRatioModeFit, HXCAspectRatioModeFill};
    static NSString *modeNames[] = {@"适应", @"填充"};
    static NSInteger currentIndex = 0;
    
    currentIndex = (currentIndex + 1) % 2;
    
    self.player.aspectRatioMode = modes[currentIndex];
    [self.aspectRatioButton setTitle:modeNames[currentIndex] forState:UIControlStateNormal];
    self.statusLabel.text = [NSString stringWithFormat:@"📐 视频比例: %@", modeNames[currentIndex]];
}

#pragma mark - 进度条操作

- (void)progressSliderTouchDown:(UISlider *)slider {
    self.isSeeking = YES;
}

- (void)progressSliderValueChanged:(UISlider *)slider {
    if (self.isSeeking && self.player.duration > 0) {
        double position = slider.value * self.player.duration;
        self.timeLabel.text = [NSString stringWithFormat:@"%@ / %@",
                               [self formatTime:position],
                               [self formatTime:self.player.duration]];
    }
}

- (void)progressSliderTouchUp:(UISlider *)slider {
    if (self.player.duration > 0) {
        double position = slider.value * self.player.duration;
        [self.player seekToPosition:position];
        self.statusLabel.text = [NSString stringWithFormat:@"⏩ 跳转到 %@", [self formatTime:position]];
    }
    self.isSeeking = NO;
}

- (void)volumeSliderChanged:(UISlider *)slider {
    self.player.volume = slider.value;
    self.volumeLabel.text = [NSString stringWithFormat:@"音量: %d%%", (int)(slider.value * 100)];
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
        case HXCPlayerStatePlaying:
            stateText = @"▶️ 播放中";
            self.isPlaying = YES;
            [self.playPauseButton setTitle:@"暂停" forState:UIControlStateNormal];
            break;
        case HXCPlayerStatePaused:
            stateText = @"⏸ 已暂停";
            self.isPlaying = NO;
            [self.playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
            break;
        case HXCPlayerStateStopped:
            stateText = @"⏹ 已停止";
            self.isPlaying = NO;
            [self.playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
            break;
        case HXCPlayerStateError:
            stateText = @"❌ 错误";
            self.isPlaying = NO;
            [self.playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
            break;
    }
    
    self.statusLabel.text = stateText;
}

- (void)playerDidEncounterError:(NSError *)error {
    self.statusLabel.text = [NSString stringWithFormat:@"❌ 错误: %@", error.localizedDescription];
}

-(void)player:(HXCPlayerControl *)player didUpdatePosition:(double)position {
    if (self.isSeeking) {
        return;
    }
    double duration = player.duration;
    // 更新进度条和时间标签
    if (duration > 0) {
        self.progressSlider.value = position / duration;
        
        self.timeLabel.text = [NSString stringWithFormat:@"%@ / %@",
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
