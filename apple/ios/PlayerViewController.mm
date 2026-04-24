/**
 * @file PlayerViewController.mm
 * @brief iOS 播放器视图控制器实现
 */

#import "PlayerViewController.h"
#import "../HXCPlayerControl.h"  // 使用统一的播放器类
#import "../HXCVDownload/HXCVDownload.h"
#import "HXCVDownloadViewController.h"
#import "../HXCPlayerLicenseManager.h"
#define HXCVOD_LICENSE_URL @"https://console-api.huaxiacloud.net/license/getMobileLicense/111453136245362688"
#define HXCVOD_LICENSE_KEY @"JNlhoUFDoLeDwNJEcoCS4GxAWk3Z2b8K"
//com.nuoshan.app

@interface PlayerViewController () <HXCPlayerControlDelegate>

@property (nonatomic, strong) HXCPlayerControl *player;
@property (nonatomic, strong) UIView *playerContainerView;
@property (nonatomic, strong) UISlider *progressSlider;
@property (nonatomic, strong) UIButton *playPauseButton;
@property (nonatomic, strong) UIButton *downloadButton;
@property (nonatomic, strong) UIButton *completedDownloadsButton;
@property (nonatomic, strong) UILabel *timeLabel;
@property (nonatomic, strong) UIButton *speedButton;
@property (nonatomic, strong) UIButton *aspectRatioButton;  // 显示模式按钮
@property (nonatomic, strong) UIButton *pipButton;  // 画中画按钮
@property (nonatomic, strong) UIButton *replayButton;  // 重播按钮
@property (nonatomic, strong) UISlider *volumeSlider;

@property (nonatomic, assign) BOOL isSeeking;

@end

@implementation PlayerViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.view.backgroundColor = [UIColor blackColor];
    
    // 创建播放器
    _player = [[HXCPlayerControl alloc] init];
    _player.decodeMode = HXCPlayerDecodeModeSoftware;
    _player.startPosition = 120;
    if (@available(iOS 14.2, *)) {
        _player.canStartPictureInPictureAutomaticallyFromInline = NO;
    }
    
    _player.delegate = self;
    
    [self setupUI];
    
    [HXCPlayerLicenseManager checkLicenseWithLicenseKey:HXCVOD_LICENSE_KEY licenseURL:HXCVOD_LICENSE_URL completionHandler:^(BOOL success, NSError * _Nullable error) {
        if (error) {
            NSLog(@"license check faild...");
            return;
        }
        // 自动播放测试视频
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self openTestVideo];
        });
    }];
    
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
    _playPauseButton.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    _playPauseButton.titleLabel.adjustsFontSizeToFitWidth = YES;
    _playPauseButton.titleLabel.minimumScaleFactor = 0.75;
    [_playPauseButton addTarget:self action:@selector(playPauseButtonTapped:) forControlEvents:UIControlEventTouchUpInside];

    _downloadButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_downloadButton setTitle:@"下载" forState:UIControlStateNormal];
    _downloadButton.tintColor = [UIColor whiteColor];
    _downloadButton.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    _downloadButton.titleLabel.adjustsFontSizeToFitWidth = YES;
    _downloadButton.titleLabel.minimumScaleFactor = 0.75;
    [_downloadButton addTarget:self action:@selector(downloadButtonTapped:) forControlEvents:UIControlEventTouchUpInside];

    _completedDownloadsButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_completedDownloadsButton setTitle:@"已下载" forState:UIControlStateNormal];
    _completedDownloadsButton.tintColor = [UIColor whiteColor];
    _completedDownloadsButton.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    _completedDownloadsButton.titleLabel.adjustsFontSizeToFitWidth = YES;
    _completedDownloadsButton.titleLabel.minimumScaleFactor = 0.75;
    [_completedDownloadsButton addTarget:self action:@selector(completedDownloadsButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    
    // 倍速 / 渲染 / PiP（右侧一组）
    _speedButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_speedButton setTitle:@"1.0x" forState:UIControlStateNormal];
    _speedButton.tintColor = [UIColor whiteColor];
    _speedButton.titleLabel.adjustsFontSizeToFitWidth = YES;
    _speedButton.titleLabel.minimumScaleFactor = 0.75;
    [_speedButton addTarget:self action:@selector(speedButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    
    _aspectRatioButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_aspectRatioButton setTitle:@"适应" forState:UIControlStateNormal];
    _aspectRatioButton.tintColor = [UIColor whiteColor];
    _aspectRatioButton.titleLabel.adjustsFontSizeToFitWidth = YES;
    _aspectRatioButton.titleLabel.minimumScaleFactor = 0.75;
    [_aspectRatioButton addTarget:self action:@selector(aspectRatioButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
    
    _pipButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_pipButton setTitle:@"PiP" forState:UIControlStateNormal];
    _pipButton.tintColor = [UIColor whiteColor];
    [_pipButton addTarget:self action:@selector(pipButtonTapped:) forControlEvents:UIControlEventTouchUpInside];

    _replayButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_replayButton setTitle:@"重播" forState:UIControlStateNormal];
    _replayButton.tintColor = [UIColor whiteColor];
    _replayButton.titleLabel.adjustsFontSizeToFitWidth = YES;
    _replayButton.titleLabel.minimumScaleFactor = 0.75;
    [_replayButton addTarget:self action:@selector(replayButtonTapped:) forControlEvents:UIControlEventTouchUpInside];

    for (UIButton *b in @[_playPauseButton, _downloadButton, _completedDownloadsButton, _replayButton, _pipButton, _speedButton, _aspectRatioButton]) {
        [b setContentHuggingPriority:UILayoutPriorityDefaultHigh forAxis:UILayoutConstraintAxisHorizontal];
        [b setContentCompressionResistancePriority:UILayoutPriorityDefaultHigh forAxis:UILayoutConstraintAxisHorizontal];
    }

    UIView *topRowSpacer = [[UIView alloc] init];
    [topRowSpacer setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    [topRowSpacer setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];

    UIStackView *topButtonRow = [[UIStackView alloc] initWithArrangedSubviews:@[
        _playPauseButton, _downloadButton, _completedDownloadsButton, topRowSpacer, _replayButton, _pipButton, _speedButton, _aspectRatioButton
    ]];
    topButtonRow.translatesAutoresizingMaskIntoConstraints = NO;
    topButtonRow.axis = UILayoutConstraintAxisHorizontal;
    topButtonRow.alignment = UIStackViewAlignmentCenter;
    topButtonRow.distribution = UIStackViewDistributionFill;
    topButtonRow.spacing = 6;
    
    // 进度条
    _progressSlider = [[UISlider alloc] init];
    _progressSlider.minimumValue = 0;
    _progressSlider.maximumValue = 1000;
    [_progressSlider addTarget:self action:@selector(progressSliderChanged:) forControlEvents:UIControlEventValueChanged];
    [_progressSlider addTarget:self action:@selector(progressSliderTouchDown:) forControlEvents:UIControlEventTouchDown];
    [_progressSlider addTarget:self action:@selector(progressSliderTouchUp:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside];
    
    // 时间标签
    _timeLabel = [[UILabel alloc] init];
    _timeLabel.text = @"00:00 / 00:00";
    _timeLabel.textColor = [UIColor whiteColor];
    _timeLabel.font = [UIFont systemFontOfSize:12];
    [_timeLabel setContentHuggingPriority:UILayoutPriorityDefaultHigh forAxis:UILayoutConstraintAxisHorizontal];
    [_timeLabel setContentCompressionResistancePriority:UILayoutPriorityDefaultHigh forAxis:UILayoutConstraintAxisHorizontal];
    
    // 音量滑块
    _volumeSlider = [[UISlider alloc] init];
    _volumeSlider.minimumValue = 0;
    _volumeSlider.maximumValue = 1.0;
    _volumeSlider.value = 1.0;
    [_volumeSlider addTarget:self action:@selector(volumeSliderChanged:) forControlEvents:UIControlEventValueChanged];
    _volumeSlider.translatesAutoresizingMaskIntoConstraints = NO;
    [[_volumeSlider.widthAnchor constraintEqualToConstant:140] setActive:YES];

    UIView *bottomRowSpacer = [[UIView alloc] init];
    [bottomRowSpacer setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];

    UIStackView *timeVolumeRow = [[UIStackView alloc] initWithArrangedSubviews:@[ _timeLabel, bottomRowSpacer, _volumeSlider ]];
    timeVolumeRow.translatesAutoresizingMaskIntoConstraints = NO;
    timeVolumeRow.axis = UILayoutConstraintAxisHorizontal;
    timeVolumeRow.alignment = UIStackViewAlignmentCenter;
    timeVolumeRow.distribution = UIStackViewDistributionFill;
    timeVolumeRow.spacing = 8;

    UIStackView *controlColumn = [[UIStackView alloc] initWithArrangedSubviews:@[ topButtonRow, _progressSlider, timeVolumeRow ]];
    controlColumn.translatesAutoresizingMaskIntoConstraints = NO;
    controlColumn.axis = UILayoutConstraintAxisVertical;
    controlColumn.alignment = UIStackViewAlignmentFill;
    controlColumn.distribution = UIStackViewDistributionFill;
    controlColumn.spacing = 8;
    [controlBar addSubview:controlColumn];
    
    // 布局约束
    [NSLayoutConstraint activateConstraints:@[
        // 播放器容器
        [_playerContainerView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [_playerContainerView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_playerContainerView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_playerContainerView.bottomAnchor constraintEqualToAnchor:controlBar.topAnchor],
        
        // 控制栏（高度由内部 Stack 决定）
        [controlBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [controlBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [controlBar.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],

        [controlColumn.leadingAnchor constraintEqualToAnchor:controlBar.leadingAnchor constant:12],
        [controlColumn.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-12],
        [controlColumn.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:12],
        [controlColumn.bottomAnchor constraintEqualToAnchor:controlBar.bottomAnchor constant:-12],
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
//    NSString *urlString = @"https://vod.tenwiseacademy.cn/111453136245362688/lf9cmlwy92fmszkjd6qaux2s7qhennhk/k43g4cz9f5c1sva3.m3u8";
//    NSString *urlString = @"https://v.shkt.online/772388bdvodtranscq1317978474/34ab23701397757895318581301/v.f1440843.mp4";
    NSString *urlString = @"https://v.shkt.online/772388bdvodtranscq1317978474/3b5133951397757895318833144/v.f1440843.mp4"; // 错误码: -1001, No such file or directory
//    NSString *urlString = @"https://v.shkt.online/772388bdvodtranscq1317978474/1e40e986388912589374781473/v.f1440843.mp4";
#if 0
    // ✨ 选择数据源模式（推荐使用新接口）
    HXCPlayerDataSourceMode mode = HXCPlayerDataSourceModeDefault;  // 或者 HXCPlayerDataSourceModeDefault
    NSLog(@"========================================");
    NSLog(@"🎬 打开视频");
    NSLog(@"   URL: %@", urlString);
    NSLog(@"   模式: %@", mode == HXCPlayerDataSourceModeDefault ? @"默认" : @"自定义HTTP");
    NSLog(@"========================================");
    HXCPlayerDataSourcePlayModel *model = [[HXCPlayerDataSourcePlayModel alloc] init];
    model.url = urlString;
    model.encryptedFile = NO;
    model.mode = mode;
    // 使用统一接口打开（底层自动处理数据源创建）
    BOOL success = [_player playWithModel:model];
#else
    BOOL success = [_player playURL:urlString];
#endif
    if (success) {
        NSLog(@"✅ 视频打开成功");
    } else {
        NSLog(@"❌ 视频打开失败");
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
    // 切换播放速度：1.0x -> 1.5x -> 2.0x -> 3.0x -> 0.5x -> 1.0x
    double currentRate = _player.playbackRate;
    double newRate = 1.0;
    
    if (currentRate == 1.0) {
        newRate = 1.5;
    } else if (currentRate == 1.5) {
        newRate = 2.0;
    } else if (currentRate == 2.0) {
        newRate = 3.0;
    } else if (currentRate == 3.0) {
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

- (void)replayButtonTapped:(UIButton *)sender {
    [_player replay];
    [_playPauseButton setTitle:@"暂停" forState:UIControlStateNormal];
}

- (void)downloadButtonTapped:(UIButton *)sender {
    (void)sender;
    HXCVDownloadViewController *vc = [[HXCVDownloadViewController alloc] init];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:vc];
    nav.modalPresentationStyle = UIModalPresentationPageSheet;
    [self presentViewController:nav animated:YES completion:nil];
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

- (void)completedDownloadsButtonTapped:(UIButton *)sender {
    NSArray<HXCVDownloadItem *> *items = [[HXCVDownloadManager sharedManager] tasksWithState:HXCVDownloadStateCompleted];
    if (items.count == 0) {
        UIAlertController *ac = [UIAlertController alertControllerWithTitle:@"已下载"
                                                                     message:@"暂无已完成下载"
                                                              preferredStyle:UIAlertControllerStyleAlert];
        [ac addAction:[UIAlertAction actionWithTitle:@"好" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:ac animated:YES completion:nil];
        return;
    }
    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"选择已下载视频"
                                                                      message:nil
                                                               preferredStyle:UIAlertControllerStyleActionSheet];
    for (HXCVDownloadItem *it in items) {
        NSString *title = [self hxdvd_displayTitleForDownloadItem:it];
        [sheet addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *_Nonnull action) {
            (void)action;
            [self hxdvd_playCompletedItem:it];
        }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil]];
    if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
        sheet.popoverPresentationController.sourceView = sender;
        sheet.popoverPresentationController.sourceRect = sender.bounds;
    }
    [self presentViewController:sheet animated:YES completion:nil];
}

- (void)hxdvd_playCompletedItem:(HXCVDownloadItem *)item {
    NSURL *fileURL = [[HXCVDownloadManager sharedManager] playableFileURLForCompletedItem:item];
    if (!fileURL) {
        UIAlertController *ac = [UIAlertController alertControllerWithTitle:@"无法播放"
                                                                     message:@"本地文件不存在或路径无效"
                                                              preferredStyle:UIAlertControllerStyleAlert];
        [ac addAction:[UIAlertAction actionWithTitle:@"好" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:ac animated:YES completion:nil];
        return;
    }
    [_player stop];
    BOOL success = [_player playURL:fileURL.path];
    if (success) {
        [_playPauseButton setTitle:@"暂停" forState:UIControlStateNormal];
    } else {
        UIAlertController *ac = [UIAlertController alertControllerWithTitle:@"打开失败"
                                                                     message:@"无法打开本地文件"
                                                              preferredStyle:UIAlertControllerStyleAlert];
        [ac addAction:[UIAlertAction actionWithTitle:@"好" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:ac animated:YES completion:nil];
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
        case HXCPlayerStateLoading:
            [_playPauseButton setTitle:@"暂停" forState:UIControlStateNormal];
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
//    NSLog(@"didUpdatePosition: %f", position);
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
//    NSLog(@"缓冲进度: %.2f", position);
}

#pragma mark - Picture in Picture Delegate

-(void)player:(HXCPlayerControl *)player didChangeLoadingState:(BOOL)isLoading {
    if (isLoading) {
        NSLog(@"正在加载中");
    } else {
        NSLog(@"加载完成");
    }
}

-(void)player:(HXCPlayerControl *)player pictureInPictureStateDidChange:(HXCPlayerPIPState)state {
    switch (state) {
        case HXCPlayerPIPStateWillStart:
            NSLog(@"📺 画中画即将开始");
            break;
        case HXCPlayerPIPStateDidStart:
            NSLog(@"✅ 画中画已开始");
            break;
        case HXCPlayerPIPStateWillStop:
            NSLog(@"📺 画中画即将停止");
            break;
        case HXCPlayerPIPStateDidStop:
            NSLog(@"✅ 画中画已停止");
            break;
        case HXCPlayerPIPStateRestore:
            NSLog(@"🔄 从画中画恢复用户界面");
            break;
            
        default:
            break;
    }
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
