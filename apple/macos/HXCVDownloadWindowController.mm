/**
 * @file HXCVDownloadWindowController.mm
 */

#import "HXCVDownloadWindowController.h"
#import "../HXCVDownload/HXCVDownload.h"

static NSString *HXCVDDownloadStateTitle(HXCVDownloadState s) {
    switch (s) {
        case HXCVDownloadStateWaiting: return @"等待";
        case HXCVDownloadStateRunning: return @"下载中";
        case HXCVDownloadStatePaused: return @"已暂停";
        case HXCVDownloadStateFailed: return @"失败";
        case HXCVDownloadStateCompleted: return @"已完成";
        case HXCVDownloadStateCancelled: return @"已取消";
    }
    return @"—";
}

static NSArray<HXCVDownloadItem *> *HXCVDDStableSortedTasks(NSArray<HXCVDownloadItem *> *tasks) {
    return [tasks sortedArrayUsingComparator:^NSComparisonResult(HXCVDownloadItem *a, HXCVDownloadItem *b) {
        NSComparisonResult c = [b.createdAt compare:a.createdAt];
        if (c != NSOrderedSame) {
            return c;
        }
        return [a.taskId compare:b.taskId];
    }];
}

@interface HXCVDownloadWindowController () <NSTableViewDataSource, NSTableViewDelegate, HXCVDownloadManagerDelegate>

@property (nonatomic, strong) NSTextField *urlField;
@property (nonatomic, strong) NSButton *addButton;
@property (nonatomic, strong) NSScrollView *scrollView;
@property (nonatomic, strong) NSTableView *tableView;
@property (nonatomic, strong) NSButton *pauseButton;
@property (nonatomic, strong) NSButton *resumeButton;
@property (nonatomic, strong) NSButton *cancelButton;
@property (nonatomic, strong) NSButton *deleteButton;
@property (nonatomic, strong) NSMutableArray<HXCVDownloadItem *> *items;

@end

@implementation HXCVDownloadWindowController

- (instancetype)init {
    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSRect rect = NSMakeRect(0, 0, 820, 480);
    NSWindow *win = [[NSWindow alloc] initWithContentRect:rect
                                                  styleMask:mask
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    win.title = @"下载管理";
    win.minSize = NSMakeSize(560, 320);
    self = [super initWithWindow:win];
    if (self) {
        _items = [NSMutableArray array];
        [self buildUI];
        [HXCVDownloadManager sharedManager].delegate = self;
        [HXCVDownloadManager sharedManager].maxConcurrentDownloads = 5;
        [self reloadFromManager];
        NSLog(@"storeDir:%@", [HXCVDownloadManager sharedManager].downloadRootDirectoryURL);
    }
    return self;
}

- (void)dealloc {
    if ([HXCVDownloadManager sharedManager].delegate == (id)self) {
        [HXCVDownloadManager sharedManager].delegate = nil;
    }
}

- (void)showDownloadPanel {
    [self.window center];
    [self showWindow:nil];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self reloadFromManager];
}

- (nullable NSString *)preservedSelectedTaskId {
    NSInteger row = self.tableView.selectedRow;
    if (row < 0 || (NSUInteger)row >= self.items.count) {
        return nil;
    }
    return self.items[(NSUInteger)row].taskId;
}

- (void)restoreTableSelectionForTaskId:(nullable NSString *)taskId {
    if (!taskId.length) {
        return;
    }
    NSUInteger idx = NSNotFound;
    for (NSUInteger i = 0; i < self.items.count; i++) {
        if ([self.items[i].taskId isEqualToString:taskId]) {
            idx = i;
            break;
        }
    }
    if (idx == NSNotFound) {
        return;
    }
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:idx] byExtendingSelection:NO];
}

- (void)reloadFromManager {
    NSString *selTaskId = [self preservedSelectedTaskId];
    NSArray *all = [[HXCVDownloadManager sharedManager] allTasks];
    [self.items removeAllObjects];
    [self.items addObjectsFromArray:HXCVDDStableSortedTasks(all)];
    [self.tableView reloadData];
    [self restoreTableSelectionForTaskId:selTaskId];
}

- (void)applyProgressSnapshotForItem:(HXCVDownloadItem *)fresh {
    NSString *tid = fresh.taskId;
    if (!tid.length) {
        return;
    }
    NSString *selTaskId = [self preservedSelectedTaskId];
    NSUInteger idx = NSNotFound;
    for (NSUInteger i = 0; i < self.items.count; i++) {
        if ([self.items[i].taskId isEqualToString:tid]) {
            idx = i;
            break;
        }
    }
    if (idx == NSNotFound) {
        [self reloadFromManager];
        return;
    }
    [self.items replaceObjectAtIndex:idx withObject:fresh];
    if (self.tableView.numberOfColumns < 1) {
        [self.tableView reloadData];
        [self restoreTableSelectionForTaskId:selTaskId];
        return;
    }
    NSIndexSet *rows = [NSIndexSet indexSetWithIndex:idx];
    NSIndexSet *cols = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, (NSUInteger)self.tableView.numberOfColumns)];
    [self.tableView reloadDataForRowIndexes:rows columnIndexes:cols];
    [self restoreTableSelectionForTaskId:selTaskId];
}

#pragma mark - UI

- (void)buildUI {
    NSView *content = self.window.contentView;
    content.translatesAutoresizingMaskIntoConstraints = NO;

    _urlField = [[NSTextField alloc] init];
    _urlField.placeholderString = @"https://example.com/video.mp4 或 m3u8 地址";
    _urlField.font = [NSFont systemFontOfSize:13];
    [_urlField setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [_urlField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    _urlField.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:_urlField];

    _addButton = [[NSButton alloc] init];
    _addButton.title = @"添加下载";
    _addButton.bezelStyle = NSBezelStyleRounded;
    _addButton.target = self;
    _addButton.action = @selector(addDownload:);
    _addButton.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:_addButton];

    _scrollView = [[NSScrollView alloc] init];
    _scrollView.hasVerticalScroller = YES;
    _scrollView.autohidesScrollers = YES;
    _scrollView.borderType = NSBezelBorder;
    _scrollView.drawsBackground = YES;
    _scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    [_scrollView setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                   forOrientation:NSLayoutConstraintOrientationVertical];
    [content addSubview:_scrollView];

    _tableView = [[NSTableView alloc] init];
    _tableView.delegate = self;
    _tableView.dataSource = self;
    _tableView.usesAlternatingRowBackgroundColors = YES;
    _tableView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;

    NSArray<NSDictionary *> *cols = @[
        @{ @"id": @"url", @"title": @"地址", @"width": @280 },
        @{ @"id": @"type", @"title": @"类型", @"width": @72 },
        @{ @"id": @"state", @"title": @"状态", @"width": @72 },
        @{ @"id": @"progress", @"title": @"进度", @"width": @56 },
        @{ @"id": @"path", @"title": @"本地路径", @"width": @220 },
    ];
    for (NSDictionary *d in cols) {
        NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:d[@"id"]];
        col.title = d[@"title"];
        col.width = [d[@"width"] doubleValue];
        col.minWidth = 40;
        [_tableView addTableColumn:col];
    }

    _scrollView.documentView = _tableView;

    // 底部操作条：固定高度，避免与 NSScrollView 在垂直方向约束竞争时表头区域与按钮重叠
    NSView *buttonBar = [[NSView alloc] init];
    buttonBar.translatesAutoresizingMaskIntoConstraints = NO;
    [buttonBar setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                      forOrientation:NSLayoutConstraintOrientationVertical];
    [content addSubview:buttonBar];

    _pauseButton = [self toolbarButton:@"暂停" action:@selector(pauseSelected:) parent:buttonBar];
    _resumeButton = [self toolbarButton:@"继续" action:@selector(resumeSelected:) parent:buttonBar];
    _cancelButton = [self toolbarButton:@"取消" action:@selector(cancelSelected:) parent:buttonBar];
    _deleteButton = [self toolbarButton:@"删除" action:@selector(deleteSelected:) parent:buttonBar];
    NSButton *refreshButton = [self toolbarButton:@"刷新" action:@selector(refresh:) parent:buttonBar];

    [NSLayoutConstraint activateConstraints:@[
        [_urlField.topAnchor constraintEqualToAnchor:content.topAnchor constant:16],
        [_urlField.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [_urlField.widthAnchor constraintGreaterThanOrEqualToConstant:200],

        [_addButton.centerYAnchor constraintEqualToAnchor:_urlField.centerYAnchor],
        [_addButton.leadingAnchor constraintEqualToAnchor:_urlField.trailingAnchor constant:12],
        [_addButton.widthAnchor constraintEqualToConstant:100],
        [_addButton.trailingAnchor constraintLessThanOrEqualToAnchor:content.trailingAnchor constant:-16],

        [buttonBar.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [buttonBar.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [buttonBar.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
        [buttonBar.heightAnchor constraintEqualToConstant:40],

        [_scrollView.topAnchor constraintEqualToAnchor:_urlField.bottomAnchor constant:12],
        [_scrollView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [_scrollView.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:16],
        [_scrollView.bottomAnchor constraintEqualToAnchor:buttonBar.topAnchor constant:-12],

        [_pauseButton.leadingAnchor constraintEqualToAnchor:buttonBar.leadingAnchor],
        [_pauseButton.centerYAnchor constraintEqualToAnchor:buttonBar.centerYAnchor],
        [_pauseButton.widthAnchor constraintEqualToConstant:72],
        [_pauseButton.heightAnchor constraintEqualToConstant:28],

        [_resumeButton.leadingAnchor constraintEqualToAnchor:_pauseButton.trailingAnchor constant:8],
        [_resumeButton.centerYAnchor constraintEqualToAnchor:buttonBar.centerYAnchor],
        [_resumeButton.widthAnchor constraintEqualToConstant:72],
        [_resumeButton.heightAnchor constraintEqualToConstant:28],

        [_cancelButton.leadingAnchor constraintEqualToAnchor:_resumeButton.trailingAnchor constant:8],
        [_cancelButton.centerYAnchor constraintEqualToAnchor:buttonBar.centerYAnchor],
        [_cancelButton.widthAnchor constraintEqualToConstant:72],
        [_cancelButton.heightAnchor constraintEqualToConstant:28],

        [_deleteButton.leadingAnchor constraintEqualToAnchor:_cancelButton.trailingAnchor constant:8],
        [_deleteButton.centerYAnchor constraintEqualToAnchor:buttonBar.centerYAnchor],
        [_deleteButton.widthAnchor constraintEqualToConstant:72],
        [_deleteButton.heightAnchor constraintEqualToConstant:28],

        [refreshButton.leadingAnchor constraintEqualToAnchor:_deleteButton.trailingAnchor constant:16],
        [refreshButton.centerYAnchor constraintEqualToAnchor:buttonBar.centerYAnchor],
        [refreshButton.widthAnchor constraintEqualToConstant:72],
        [refreshButton.heightAnchor constraintEqualToConstant:28],
    ]];

    NSLayoutConstraint *minTableH = [_scrollView.heightAnchor constraintGreaterThanOrEqualToConstant:120];
    minTableH.priority = NSLayoutPriorityDefaultHigh;
    minTableH.active = YES;
}

- (NSButton *)toolbarButton:(NSString *)title action:(SEL)action parent:(NSView *)parent {
    NSButton *b = [[NSButton alloc] init];
    b.title = title;
    b.bezelStyle = NSBezelStyleRounded;
    b.target = self;
    b.action = action;
    b.translatesAutoresizingMaskIntoConstraints = NO;
    [parent addSubview:b];
    return b;
}

#pragma mark - Actions

- (void)addDownload:(id)sender {
    NSString *raw = [_urlField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (raw.length == 0) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"请输入下载地址";
        a.alertStyle = NSAlertStyleInformational;
        [a addButtonWithTitle:@"确定"];
        [a runModal];
        return;
    }
    NSURL *url = [NSURL URLWithString:raw];
    if (!url || !url.scheme.length) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"地址格式无效";
        a.informativeText = @"请输入完整的 http(s) URL";
        [a addButtonWithTitle:@"确定"];
        [a runModal];
        return;
    }
    NSError *err = nil;
    NSString *tid = [[HXCVDownloadManager sharedManager] enqueueDownloadWithURL:url error:&err];
    if (!tid) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"添加失败";
        a.informativeText = err.localizedDescription ?: @"未知错误";
        [a addButtonWithTitle:@"确定"];
        [a runModal];
        return;
    }
    [self reloadFromManager];
}

- (nullable HXCVDownloadItem *)selectedItem {
    NSInteger row = self.tableView.selectedRow;
    if (row < 0 || row >= (NSInteger)self.items.count) {
        return nil;
    }
    return self.items[(NSUInteger)row];
}

- (void)pauseSelected:(id)sender {
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) {
        return;
    }
    NSError *e = nil;
    [[HXCVDownloadManager sharedManager] pauseTaskId:item.taskId error:&e];
    [self reloadFromManager];
}

- (void)resumeSelected:(id)sender {
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) {
        return;
    }
    NSError *e = nil;
    [[HXCVDownloadManager sharedManager] resumeTaskId:item.taskId error:&e];
    [self reloadFromManager];
}

- (void)cancelSelected:(id)sender {
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) {
        return;
    }
    NSError *e = nil;
    [[HXCVDownloadManager sharedManager] cancelTaskId:item.taskId error:&e];
    [self reloadFromManager];
}

- (void)deleteSelected:(id)sender {
    HXCVDownloadItem *item = [self selectedItem];
    if (!item) {
        return;
    }
    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = @"删除下载任务？";
    confirm.informativeText = @"将同时删除本地已下载文件（若有）。";
    [confirm addButtonWithTitle:@"删除"];
    [confirm addButtonWithTitle:@"取消"];
    confirm.alertStyle = NSAlertStyleWarning;
    if ([confirm runModal] != NSAlertFirstButtonReturn) {
        return;
    }
    NSError *e = nil;
    [[HXCVDownloadManager sharedManager] deleteTaskId:item.taskId removeFiles:YES error:&e];
    [self reloadFromManager];
}

- (void)refresh:(id)sender {
    [self reloadFromManager];
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
    NSLog(@"download failed: %@", error.localizedDescription);
}

- (void)downloadManager:(HXCVDownloadManager *)manager didUpdateProgressForItem:(HXCVDownloadItem *)item {
    (void)manager;
    [self applyProgressSnapshotForItem:item];
}

- (void)downloadManager:(HXCVDownloadManager *)manager didCompleteDownloadForItem:(HXCVDownloadItem *)item {
    (void)manager;
    (void)item;
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return (NSInteger)self.items.count;
}

- (nullable id)tableView:(NSTableView *)tableView objectValueForTableColumn:(nullable NSTableColumn *)tableColumn row:(NSInteger)row {
    (void)tableView;
    if (row < 0 || row >= (NSInteger)self.items.count) {
        return nil;
    }
    HXCVDownloadItem *item = self.items[(NSUInteger)row];
    NSString *cid = tableColumn.identifier;
    if ([cid isEqualToString:@"url"]) {
        NSString *u = item.urlString;
        if (u.length > 48) {
            return [[u substringToIndex:45] stringByAppendingString:@"…"];
        }
        return u;
    }
    if ([cid isEqualToString:@"type"]) {
        return item.downloadType == HXCVDownloadTypeHLS ? @"HLS" : @"直链";
    }
    if ([cid isEqualToString:@"state"]) {
        return HXCVDDownloadStateTitle(item.state);
    }
    if ([cid isEqualToString:@"progress"]) {
        if (item.state == HXCVDownloadStateCompleted) {
            return @"100%";
        }
        return [NSString stringWithFormat:@"%.1f%%", item.progress * 100.0];
    }
    if ([cid isEqualToString:@"path"]) {
        NSString *p = item.localRelativePath;
        return p.length ? p : @"—";
    }
    return @"";
}

@end
