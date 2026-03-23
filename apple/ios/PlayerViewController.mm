/**
 * @file PlayerViewController.mm
 * @brief iOS 播放器视图控制器实现
 */

#import "PlayerViewController.h"
#import "../HXCPlayerControl.h"  // 使用统一的播放器类

//com.nuoshan.app

@interface PlayerViewController () <HXCPlayerControlDelegate>

@property (nonatomic, strong) HXCPlayerControl *player;
@property (nonatomic, strong) UIView *playerContainerView;
@property (nonatomic, strong) UISlider *progressSlider;
@property (nonatomic, strong) UIButton *playPauseButton;
@property (nonatomic, strong) UILabel *timeLabel;
@property (nonatomic, strong) UIButton *speedButton;
@property (nonatomic, strong) UIButton *aspectRatioButton;  // 显示模式按钮
@property (nonatomic, strong) UIButton *pipButton;  // 画中画按钮
@property (nonatomic, strong) UISlider *volumeSlider;

@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation PlayerViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.view.backgroundColor = [UIColor blackColor];
    
    // 创建播放器
    _player = [[HXCPlayerControl alloc] init];
    _player.startPosition = 67;
    if (@available(iOS 14.2, *)) {
        _player.canStartPictureInPictureAutomaticallyFromInline = YES;
    }
    
    _player.delegate = self;
    
    [self setupUI];
    
    // 自动播放测试视频
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self openTestVideo];
    });
}

- (void)setupUI {
    // 播放器容器视图
    _playerContainerView = [[UIView alloc] init];
    _playerContainerView.backgroundColor = [UIColor blackColor];
    _playerContainerView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_playerContainerView];
    
    // 添加视频视图
    _player.videoView.frame = _playerContainerView.bounds;
    _player.videoView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [_playerContainerView addSubview:_player.videoView];
    
    // 控制栏容器
    UIView *controlBar = [[UIView alloc] init];
    controlBar.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.7];
    controlBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:controlBar];
    
    // 播放/暂停按钮
    _playPauseButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
    _playPauseButton.tintColor = [UIColor whiteColor];
    _playPauseButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_playPauseButton addTarget:self action:@selector(playPauseButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    [controlBar addSubview:_playPauseButton];
    
    // 进度条
    _progressSlider = [[UISlider alloc] init];
    _progressSlider.minimumValue = 0;
    _progressSlider.maximumValue = 1000;
    _progressSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [_progressSlider addTarget:self action:@selector(progressSliderChanged:) forControlEvents:UIControlEventValueChanged];
    [_progressSlider addTarget:self action:@selector(progressSliderTouchDown:) forControlEvents:UIControlEventTouchDown];
    [_progressSlider addTarget:self action:@selector(progressSliderTouchUp:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside];
    [controlBar addSubview:_progressSlider];
    
    // 时间标签
    _timeLabel = [[UILabel alloc] init];
    _timeLabel.text = @"00:00 / 00:00";
    _timeLabel.textColor = [UIColor whiteColor];
    _timeLabel.font = [UIFont systemFontOfSize:12];
    _timeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [controlBar addSubview:_timeLabel];
    
    // 倍速按钮
    _speedButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_speedButton setTitle:@"1.0x" forState:UIControlStateNormal];
    _speedButton.tintColor = [UIColor whiteColor];
    _speedButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_speedButton addTarget:self action:@selector(speedButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    [controlBar addSubview:_speedButton];
    
    // 显示模式按钮
    _aspectRatioButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_aspectRatioButton setTitle:@"适应" forState:UIControlStateNormal];
    _aspectRatioButton.tintColor = [UIColor whiteColor];
    _aspectRatioButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_aspectRatioButton addTarget:self action:@selector(aspectRatioButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    [controlBar addSubview:_aspectRatioButton];
    
    // 画中画按钮
    _pipButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_pipButton setTitle:@"PiP" forState:UIControlStateNormal];
    _pipButton.tintColor = [UIColor whiteColor];
    _pipButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_pipButton addTarget:self action:@selector(pipButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    [controlBar addSubview:_pipButton];
    
    // 音量滑块
    _volumeSlider = [[UISlider alloc] init];
    _volumeSlider.minimumValue = 0;
    _volumeSlider.maximumValue = 1.0;
    _volumeSlider.value = 1.0;
    _volumeSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [_volumeSlider addTarget:self action:@selector(volumeSliderChanged:) forControlEvents:UIControlEventValueChanged];
    [controlBar addSubview:_volumeSlider];
    
    // 布局约束
    [NSLayoutConstraint activateConstraints:@[
        // 播放器容器
        [_playerContainerView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [_playerContainerView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_playerContainerView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_playerContainerView.bottomAnchor constraintEqualToAnchor:controlBar.topAnchor],
        
        // 控制栏
        [controlBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [controlBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [controlBar.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [controlBar.heightAnchor constraintEqualToConstant:120],
        
        // 播放按钮
        [_playPauseButton.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:16],
        [_playPauseButton.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:16],
        [_playPauseButton.widthAnchor constraintEqualToConstant:60],
        
        // 显示模式按钮（在右上角）
        [_aspectRatioButton.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-16],
        [_aspectRatioButton.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:16],
        [_aspectRatioButton.widthAnchor constraintEqualToConstant:60],
        
        // 倍速按钮（在显示模式按钮左边）
        [_speedButton.trailingAnchor constraintEqualToAnchor:_aspectRatioButton.leadingAnchor constant:-8],
        [_speedButton.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:16],
        [_speedButton.widthAnchor constraintEqualToConstant:60],
        
        // 画中画按钮（在倍速按钮左边）
        [_pipButton.trailingAnchor constraintEqualToAnchor:_speedButton.leadingAnchor constant:-8],
        [_pipButton.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:16],
        [_pipButton.widthAnchor constraintEqualToConstant:60],
        
        // 进度条
        [_progressSlider.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:16],
        [_progressSlider.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-16],
        [_progressSlider.topAnchor constraintEqualToAnchor:_playPauseButton.bottomAnchor constant:8],
        
        // 时间标签
        [_timeLabel.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:16],
        [_timeLabel.topAnchor constraintEqualToAnchor:_progressSlider.bottomAnchor constant:8],
        
        // 音量滑块
        [_volumeSlider.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-16],
        [_volumeSlider.topAnchor constraintEqualToAnchor:_progressSlider.bottomAnchor constant:8],
        [_volumeSlider.widthAnchor constraintEqualToConstant:150],
    ]];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
//    _player.videoLayer.frame = _playerContainerView.bounds;
}

- (void)dealloc {
    [_player stop];
}

#pragma mark - Test Video

- (void)openTestVideo {
    // 测试网络视频
//        NSString *urlString = @"https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4";
//    NSString *urlString = @"https://vod-volcengine.cskziwl.cn/P6N8MWsjc58A5Rb3/K7XpsqzzPY1dGv5f.mp4";
//    NSString *urlString = @"https://v.shkt.online/772388bdvodtranscq1317978474/4ece4b555145403697569546683/v.f1440843.mp4";//h265
    NSString *urlString = @"https://vod.tenwiseacademy.cn/111453136245362688/lf9cmlwy92fmszkjd6qaux2s7qhennhk/k43g4cz9f5c1sva3.m3u8";
//    NSString *urlString = @"https://f18c14f8-vod-tx-cdn-cskziwl-cn.tliveapp.com/1/47/mnt/g/file/20250930/b/o/u/c6ae79da0c546e0e/k43g4cz9f5c1sva3.m3u8";
    BOOL success = [_player openURL:urlString];
    if (success) {
        NSLog(@"视频打开成功");
        [_player play];
    } else {
        NSLog(@"视频打开失败");
    }
}

#pragma mark - Control Actions

- (void)playPauseButtonTapped:(UIButton *)sender {
    if (_player.state == HXCPlayerStatePlaying) {
        [_player pause];
        [sender setTitle:@"播放" forState:UIControlStateNormal];
    } else {
        [_player play];
        [sender setTitle:@"暂停" forState:UIControlStateNormal];
    }
}

- (void)progressSliderTouchDown:(UISlider *)sender {
    _isSeeking = YES;
}

- (void)progressSliderChanged:(UISlider *)sender {
    if (!_isSeeking) {
        return;
    }
    
    double duration = _player.duration;
    double position = (sender.value / 1000.0) * duration;
    _timeLabel.text = [NSString stringWithFormat:@"%@ / %@",
                      [self formatTime:position],
                      [self formatTime:duration]];
}

- (void)progressSliderTouchUp:(UISlider *)sender {
    double duration = _player.duration;
    double position = (sender.value / 1000.0) * duration;
    [_player seekToPosition:position];
    _isSeeking = NO;
}

- (void)speedButtonTapped:(UIButton *)sender {
    // 切换播放速度：1.0x -> 1.5x -> 2.0x -> 0.5x -> 1.0x
    double currentRate = _player.playbackRate;
    double newRate = 1.0;
    
    if (currentRate == 1.0) {
        newRate = 1.5;
    } else if (currentRate == 1.5) {
        newRate = 2.0;
    } else if (currentRate == 2.0) {
        newRate = 0.5;
    } else {
        newRate = 1.0;
    }
    
    _player.playbackRate = newRate;
    [sender setTitle:[NSString stringWithFormat:@"%.1fx", newRate] forState:UIControlStateNormal];
}

- (void)aspectRatioButtonTapped:(UIButton *)sender {
    // 切换显示模式：Fit（适应）<-> Fill（填充）
    if (_player.aspectRatioMode == HXCAspectRatioModeFit) {
        _player.aspectRatioMode = HXCAspectRatioModeFill;
        [sender setTitle:@"填充" forState:UIControlStateNormal];
    } else {
        _player.aspectRatioMode = HXCAspectRatioModeFit;
        [sender setTitle:@"适应" forState:UIControlStateNormal];
    }
}

- (void)volumeSliderChanged:(UISlider *)sender {
    _player.volume = sender.value;
}

- (void)pipButtonTapped:(UIButton *)sender {
    // 切换画中画模式
    if ([_player isPictureInPictureActive]) {
        [_player stopPictureInPicture];
    } else {
        [_player startPictureInPicture];
    }
}

#pragma mark - HXCPlayerControlDelegate

// 状态变化
- (void)player:(HXCPlayerControl *)player didChangeState:(HXCPlayerState)state {
    NSLog(@"播放器状态改变: %ld", (long)state);
    
    switch (state) {
        case HXCPlayerStatePlaying:
            [_playPauseButton setTitle:@"暂停" forState:UIControlStateNormal];
            break;
        case HXCPlayerStatePaused:
            [_playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
            break;
        case HXCPlayerStateStopped:
            [_playPauseButton setTitle:@"播放" forState:UIControlStateNormal];
            _progressSlider.value = 0;
            _timeLabel.text = @"00:00 / 00:00";
            break;
        default:
            break;
    }
}

// 错误通知
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error {
    NSLog(@"播放器错误: %@", error.localizedDescription);
    
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"错误"
                                                                   message:error.localizedDescription
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

// 播放进度更新（真实播放位置）
- (void)player:(HXCPlayerControl *)player didUpdatePosition:(double)position {
    if (_isSeeking) {
        return;  // 拖动时不更新进度条
    }
    
    double duration = player.duration;
    
    if (duration > 0) {
        _progressSlider.value = (position / duration) * 1000;
        _timeLabel.text = [NSString stringWithFormat:@"%@ / %@",
                          [self formatTime:position],
                          [self formatTime:duration]];
    }
}

// 缓冲进度更新（解码位置）
- (void)player:(HXCPlayerControl *)player didUpdateBufferProgress:(double)position {
    // 可以在这里显示缓冲进度条
    NSLog(@"缓冲进度: %.2f", position);
}

#pragma mark - Picture in Picture Delegate

- (void)player:(HXCPlayerControl *)player 
    pictureInPictureControllerWillStartPictureInPicture:(AVPictureInPictureController *)controller {
    NSLog(@"📺 画中画即将开始");
}

- (void)player:(HXCPlayerControl *)player 
    pictureInPictureControllerDidStartPictureInPicture:(AVPictureInPictureController *)controller {
    NSLog(@"✅ 画中画已开始");
    // 可以隐藏播放器界面
    // self.view.alpha = 0.5;
}

- (void)player:(HXCPlayerControl *)player 
    pictureInPictureControllerWillStopPictureInPicture:(AVPictureInPictureController *)controller {
    NSLog(@"📺 画中画即将停止");
}

- (void)player:(HXCPlayerControl *)player 
    pictureInPictureControllerDidStopPictureInPicture:(AVPictureInPictureController *)controller {
    NSLog(@"✅ 画中画已停止");
    // 恢复播放器界面
    // self.view.alpha = 1.0;
}

- (void)player:(HXCPlayerControl *)player 
    restoreUserInterfaceForPictureInPictureStopWithCompletionHandler:(void (^)(BOOL))completionHandler {
    NSLog(@"🔄 从画中画恢复用户界面");
    
    // 恢复应用界面（例如：导航回播放器页面）
    // 这里可以添加恢复逻辑
    
    // 完成后调用 completionHandler
    completionHandler(YES);
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
