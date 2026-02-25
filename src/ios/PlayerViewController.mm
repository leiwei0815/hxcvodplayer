/**
 * @file PlayerViewController.mm
 * @brief iOS 播放器视图控制器实现
 */

#import "PlayerViewController.h"
#import "YXPlayerControl.h"

@interface PlayerViewController () <YXPlayerControlDelegate>

@property (nonatomic, strong) YXPlayerControl *player;
@property (nonatomic, strong) UIView *playerContainerView;
@property (nonatomic, strong) UISlider *progressSlider;
@property (nonatomic, strong) UIButton *playPauseButton;
@property (nonatomic, strong) UILabel *timeLabel;
@property (nonatomic, strong) UIButton *speedButton;
@property (nonatomic, strong) UIButton *aspectRatioButton;  // 显示模式按钮
@property (nonatomic, strong) UISlider *volumeSlider;

@property (nonatomic, strong) NSTimer *progressTimer;
@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation PlayerViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.view.backgroundColor = [UIColor blackColor];
    
    // 创建播放器
    _player = [[YXPlayerControl alloc] init];
    _player.startPosition = 67;
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
    
    // 添加视频层
    _player.videoLayer.frame = _playerContainerView.bounds;
    [_playerContainerView.layer addSublayer:_player.videoLayer];
    
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
    _player.videoLayer.frame = _playerContainerView.bounds;
}

- (void)dealloc {
    [_progressTimer invalidate];
    [_player close];
}

#pragma mark - Test Video

- (void)openTestVideo {
    // 测试网络视频
    NSString *urlString = @"https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4";
    
    BOOL success = [_player openURL:urlString];
    if (success) {
        NSLog(@"视频打开成功");
        [_player play];
        [self startProgressTimer];
    } else {
        NSLog(@"视频打开失败");
    }
}

#pragma mark - Progress Timer

- (void)startProgressTimer {
    if (_progressTimer) {
        return;
    }
    
    _progressTimer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer * _Nonnull timer) {
        [self updateProgress];
    }];
}

- (void)updateProgress {
    if (_isSeeking) {
        return;
    }
    
    double position = _player.position;
    double duration = _player.duration;
    
    if (duration > 0) {
        _progressSlider.value = (position / duration) * 1000;
        _timeLabel.text = [NSString stringWithFormat:@"%@ / %@",
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

#pragma mark - Control Actions

- (void)playPauseButtonTapped:(UIButton *)sender {
    if (_player.state == YXPlayerStatePlaying) {
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
    if (_player.aspectRatioMode == YXAspectRatioModeFit) {
        _player.aspectRatioMode = YXAspectRatioModeFill;
        [sender setTitle:@"填充" forState:UIControlStateNormal];
    } else {
        _player.aspectRatioMode = YXAspectRatioModeFit;
        [sender setTitle:@"适应" forState:UIControlStateNormal];
    }
}

- (void)volumeSliderChanged:(UISlider *)sender {
    _player.volume = sender.value;
}

#pragma mark - YXPlayerControlDelegate

- (void)player:(YXPlayerControl *)player didChangeState:(YXPlayerState)state {
    NSLog(@"播放器状态改变: %ld", (long)state);
}

- (void)player:(YXPlayerControl *)player didEncounterError:(NSString *)error {
    NSLog(@"播放器错误: %@", error);
    
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"错误"
                                                                   message:error
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
