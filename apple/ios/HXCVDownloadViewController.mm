#import "HXCVDownloadViewController.h"
#import "../HXCVDownload/HXCVDownload.h"

static NSString *HXCVDIOSStateTitle(HXCVDownloadState state) {
    switch (state) {
        case HXCVDownloadStateWaiting: return @"等待";
        case HXCVDownloadStateRunning: return @"下载中";
        case HXCVDownloadStatePaused: return @"已暂停";
        case HXCVDownloadStateFailed: return @"失败";
        case HXCVDownloadStateCompleted: return @"已完成";
        case HXCVDownloadStateCancelled: return @"已取消";
    }
    return @"-";
}

/// 与 `allTasks`（按 updatedAt）不同：列表按创建时间排序，进度更新不会改变行序，便于只刷新单行。
static NSArray<HXCVDownloadItem *> *HXCVDIOSStableSortedTasks(NSArray<HXCVDownloadItem *> *tasks) {
    return [tasks sortedArrayUsingComparator:^NSComparisonResult(HXCVDownloadItem *a, HXCVDownloadItem *b) {
        NSComparisonResult c = [b.createdAt compare:a.createdAt];
        if (c != NSOrderedSame) {
            return c;
        }
        return [a.taskId compare:b.taskId];
    }];
}

@interface HXCVDownloadViewController () <UITableViewDataSource, UITableViewDelegate, UITextFieldDelegate, HXCVDownloadManagerDelegate>

@property (nonatomic, strong) UITextField *urlField;
@property (nonatomic, strong) UIButton *addButton;
@property (nonatomic, strong) UITableView *tableView;
@property (nonatomic, strong) UIButton *pauseButton;
@property (nonatomic, strong) UIButton *resumeButton;
@property (nonatomic, strong) UIButton *cancelButton;
@property (nonatomic, strong) UIButton *deleteButton;
@property (nonatomic, strong) UIButton *refreshButton;
@property (nonatomic, strong) NSMutableArray<HXCVDownloadItem *> *items;

@end

@implementation HXCVDownloadViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"下载管理";
    self.view.backgroundColor = [UIColor systemBackgroundColor];
    self.items = [NSMutableArray array];

    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemClose
                                                                                           target:self
                                                                                           action:@selector(closeTapped:)];

    [self buildUI];
    [HXCVDownloadManager sharedManager].delegate = self;
    [self reloadFromManager];
}

- (void)dealloc {
    if ([HXCVDownloadManager sharedManager].delegate == (id)self) {
        [HXCVDownloadManager sharedManager].delegate = nil;
    }
}

- (void)buildUI {
    UIStackView *topRow = [[UIStackView alloc] init];
    topRow.axis = UILayoutConstraintAxisHorizontal;
    topRow.spacing = 8;
    topRow.alignment = UIStackViewAlignmentFill;
    topRow.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:topRow];

    self.urlField = [[UITextField alloc] init];
    self.urlField.borderStyle = UITextBorderStyleRoundedRect;
    self.urlField.placeholder = @"https://example.com/video.mp4 或 m3u8 地址";
    self.urlField.clearButtonMode = UITextFieldViewModeWhileEditing;
    self.urlField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.urlField.autocorrectionType = UITextAutocorrectionTypeNo;
    self.urlField.keyboardType = UIKeyboardTypeURL;
    self.urlField.returnKeyType = UIReturnKeyDone;
    self.urlField.delegate = self;
    [topRow addArrangedSubview:self.urlField];

    self.addButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.addButton setTitle:@"添加" forState:UIControlStateNormal];
    [self.addButton addTarget:self action:@selector(addDownload:) forControlEvents:UIControlEventTouchUpInside];
    [topRow addArrangedSubview:self.addButton];

    self.tableView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
    self.tableView.translatesAutoresizingMaskIntoConstraints = NO;
    self.tableView.dataSource = self;
    self.tableView.delegate = self;
    self.tableView.rowHeight = 72;
    [self.view addSubview:self.tableView];

    UIStackView *buttonBar = [[UIStackView alloc] init];
    buttonBar.axis = UILayoutConstraintAxisHorizontal;
    buttonBar.spacing = 8;
    buttonBar.alignment = UIStackViewAlignmentFill;
    buttonBar.distribution = UIStackViewDistributionFillEqually;
    buttonBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:buttonBar];

    self.pauseButton = [self toolbarButton:@"暂停" action:@selector(pauseSelected:)];
    self.resumeButton = [self toolbarButton:@"继续" action:@selector(resumeSelected:)];
    self.cancelButton = [self toolbarButton:@"取消" action:@selector(cancelSelected:)];
    self.deleteButton = [self toolbarButton:@"删除" action:@selector(deleteSelected:)];
    self.refreshButton = [self toolbarButton:@"刷新" action:@selector(refresh:)];
    [buttonBar addArrangedSubview:self.pauseButton];
    [buttonBar addArrangedSubview:self.resumeButton];
    [buttonBar addArrangedSubview:self.cancelButton];
    [buttonBar addArrangedSubview:self.deleteButton];
    [buttonBar addArrangedSubview:self.refreshButton];

    [NSLayoutConstraint activateConstraints:@[
        [topRow.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:16],
        [topRow.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:16],
        [topRow.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-16],

        [self.addButton.widthAnchor constraintEqualToConstant:64],

        [buttonBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:16],
        [buttonBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-16],
        [buttonBar.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-16],
        [buttonBar.heightAnchor constraintEqualToConstant:36],

        [self.tableView.topAnchor constraintEqualToAnchor:topRow.bottomAnchor constant:12],
        [self.tableView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.tableView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.tableView.bottomAnchor constraintEqualToAnchor:buttonBar.topAnchor constant:-12],
    ]];
}

- (UIButton *)toolbarButton:(NSString *)title action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    button.layer.cornerRadius = 8;
    button.layer.borderWidth = 1;
    button.layer.borderColor = [UIColor systemGray4Color].CGColor;
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (nullable NSString *)preservedSelectedTaskId {
    NSIndexPath *ip = self.tableView.indexPathForSelectedRow;
    if (!ip || ip.row < 0 || (NSUInteger)ip.row >= self.items.count) {
        return nil;
    }
    return self.items[(NSUInteger)ip.row].taskId;
}

- (void)restoreTableSelectionForTaskId:(nullable NSString *)taskId {
    if (!taskId.length) {
        return;
    }
    __block NSUInteger idx = NSNotFound;
    [self.items enumerateObjectsUsingBlock:^(HXCVDownloadItem *obj, NSUInteger i, BOOL *stop) {
        if ([obj.taskId isEqualToString:taskId]) {
            idx = i;
            *stop = YES;
        }
    }];
    if (idx == NSNotFound) {
        return;
    }
    NSIndexPath *ip = [NSIndexPath indexPathForRow:idx inSection:0];
    [self.tableView selectRowAtIndexPath:ip animated:NO scrollPosition:UITableViewScrollPositionNone];
}

- (void)reloadFromManager {
    NSString *selTaskId = [self preservedSelectedTaskId];
    NSArray<HXCVDownloadItem *> *all = [[HXCVDownloadManager sharedManager] allTasks];
    [self.items removeAllObjects];
    [self.items addObjectsFromArray:HXCVDIOSStableSortedTasks(all)];
    [self.tableView reloadData];
    [self restoreTableSelectionForTaskId:selTaskId];
}

/// 仅更新一条任务的快照并刷新对应行，避免高频进度下整表 reload 闪烁。
- (void)applyProgressSnapshotForItem:(HXCVDownloadItem *)fresh {
    NSString *tid = fresh.taskId;
    if (!tid.length) {
        return;
    }
    NSString *selTaskId = [self preservedSelectedTaskId];
    __block NSUInteger idx = NSNotFound;
    [self.items enumerateObjectsUsingBlock:^(HXCVDownloadItem *obj, NSUInteger i, BOOL *stop) {
        if ([obj.taskId isEqualToString:tid]) {
            idx = i;
            *stop = YES;
        }
    }];
    if (idx == NSNotFound) {
        [self reloadFromManager];
        return;
    }
    [self.items replaceObjectAtIndex:idx withObject:fresh];
    NSIndexPath *ip = [NSIndexPath indexPathForRow:idx inSection:0];
    [UIView performWithoutAnimation:^{
        [self.tableView reloadRowsAtIndexPaths:@[ip] withRowAnimation:UITableViewRowAnimationNone];
    }];
    [self restoreTableSelectionForTaskId:selTaskId];
}

- (nullable HXCVDownloadItem *)selectedItem {
    NSIndexPath *indexPath = self.tableView.indexPathForSelectedRow;
    if (!indexPath || indexPath.row >= (NSInteger)self.items.count) {
        return nil;
    }
    return self.items[(NSUInteger)indexPath.row];
}

- (NSString *)displayTitleForItem:(HXCVDownloadItem *)item {
    NSURL *url = [NSURL URLWithString:item.urlString];
    NSString *name = url.lastPathComponent ?: @"";
    if ([name containsString:@"?"]) {
        name = [[name componentsSeparatedByString:@"?"] firstObject];
    }
    if (name.length == 0) {
        name = @"视频";
    }
    return item.downloadType == HXCVDownloadTypeHLS ? [NSString stringWithFormat:@"%@（HLS）", name] : name;
}

- (NSString *)detailTextForItem:(HXCVDownloadItem *)item {
    NSString *type = item.downloadType == HXCVDownloadTypeHLS ? @"HLS" : @"直链";
    NSString *progress = [NSString stringWithFormat:@"%.0f%%", item.progress * 100.0];
    NSString *path = item.localRelativePath.length ? item.localRelativePath : @"未落盘";
    if (item.errorMessage.length > 0 && item.state == HXCVDownloadStateFailed) {
        return [NSString stringWithFormat:@"%@ | %@ | %@ | %@", type, HXCVDIOSStateTitle(item.state), progress, item.errorMessage];
    }
    return [NSString stringWithFormat:@"%@ | %@ | %@ | %@", type, HXCVDIOSStateTitle(item.state), progress, path];
}

- (void)showAlertTitle:(NSString *)title message:(NSString *)message {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:title
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"好" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

#pragma mark - Actions

- (void)closeTapped:(id)sender {
    (void)sender;
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)addDownload:(id)sender {
    (void)sender;
    NSString *raw = [self.urlField.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (raw.length == 0) {
        [self showAlertTitle:@"请输入下载地址" message:@""];
        return;
    }
    NSURL *url = [NSURL URLWithString:raw];
    if (!url || !url.scheme.length) {
        [self showAlertTitle:@"地址格式无效" message:@"请输入完整的 http(s) URL"];
        return;
    }
    NSError *error = nil;
    NSString *taskId = [[HXCVDownloadManager sharedManager] enqueueDownloadWithURL:url error:&error];
    if (!taskId) {
        [self showAlertTitle:@"添加失败" message:error.localizedDescription ?: @"未知错误"];
        return;
    }
    self.urlField.text = @"";
    [self reloadFromManager];
}

- (void)pauseSelected:(id)sender {
    (void)sender;
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) return;
    NSError *error = nil;
    [[HXCVDownloadManager sharedManager] pauseTaskId:item.taskId error:&error];
    if (error) {
        [self showAlertTitle:@"暂停失败" message:error.localizedDescription ?: @"未知错误"];
    }
    [self reloadFromManager];
}

- (void)resumeSelected:(id)sender {
    (void)sender;
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) return;
    NSError *error = nil;
    [[HXCVDownloadManager sharedManager] resumeTaskId:item.taskId error:&error];
    if (error) {
        [self showAlertTitle:@"继续失败" message:error.localizedDescription ?: @"未知错误"];
    }
    [self reloadFromManager];
}

- (void)cancelSelected:(id)sender {
    (void)sender;
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) return;
    NSError *error = nil;
    [[HXCVDownloadManager sharedManager] cancelTaskId:item.taskId error:&error];
    if (error) {
        [self showAlertTitle:@"取消失败" message:error.localizedDescription ?: @"未知错误"];
    }
    [self reloadFromManager];
}

- (void)deleteSelected:(id)sender {
    (void)sender;
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) return;

    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"删除下载任务？"
                                                                   message:@"将同时删除本地已下载文件（若有）。"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    __weak typeof(self) weakSelf = self;
    [alert addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"删除" style:UIAlertActionStyleDestructive handler:^(UIAlertAction * _Nonnull action) {
        (void)action;
        NSError *error = nil;
        [[HXCVDownloadManager sharedManager] deleteTaskId:item.taskId removeFiles:YES error:&error];
        if (error) {
            [weakSelf showAlertTitle:@"删除失败" message:error.localizedDescription ?: @"未知错误"];
        }
        [weakSelf reloadFromManager];
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)refresh:(id)sender {
    (void)sender;
    [self reloadFromManager];
}

#pragma mark - UITextFieldDelegate

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    [textField resignFirstResponder];
    [self addDownload:textField];
    return YES;
}

#pragma mark - HXCVDownloadManagerDelegate

- (void)downloadManager:(HXCVDownloadManager *)manager didChangeStateForItem:(HXCVDownloadItem *)item previousState:(HXCVDownloadState)previousState {
    (void)manager;
    (void)item;
    (void)previousState;
    [self reloadFromManager];
}

- (void)downloadManager:(HXCVDownloadManager *)manager didFailDownloadForItem:(HXCVDownloadItem *)item error:(NSError *)error {
    (void)manager;
    (void)item;
    (void)error;
}

- (void)downloadManager:(HXCVDownloadManager *)manager didUpdateProgressForItem:(HXCVDownloadItem *)item {
    (void)manager;
    [self applyProgressSnapshotForItem:item];
}

- (void)downloadManager:(HXCVDownloadManager *)manager didCompleteDownloadForItem:(HXCVDownloadItem *)item {
    (void)manager;
    (void)item;
}

#pragma mark - UITableViewDataSource

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;
    (void)section;
    return self.items.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    static NSString *cellId = @"HXCVDownloadCell";
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:cellId];
    if (!cell) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:cellId];
        cell.textLabel.numberOfLines = 1;
        cell.detailTextLabel.numberOfLines = 2;
        cell.accessoryType = UITableViewCellAccessoryNone;
    }
    HXCVDownloadItem *item = self.items[(NSUInteger)indexPath.row];
    cell.textLabel.text = [self displayTitleForItem:item];
    cell.detailTextLabel.text = [self detailTextForItem:item];
    cell.detailTextLabel.textColor = [UIColor secondaryLabelColor];
    return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    (void)tableView;
    (void)indexPath;
}

@end
