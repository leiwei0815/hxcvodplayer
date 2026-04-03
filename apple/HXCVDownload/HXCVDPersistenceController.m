#import "HXCVDPersistenceController.h"
#import "HXCVDownloadItem+Internal.h"
#import <CoreData/CoreData.h>

static NSString *const kEntityName = @"HXCVDownloadTaskEntity";

@interface HXCVDPersistenceController ()
@property (nonatomic, strong, readwrite) NSPersistentContainer *persistentContainer;
@end

@implementation HXCVDPersistenceController

+ (NSString *)hxcvd_normalizeUserIdentifier:(nullable NSString *)identifier {
    if (!identifier) {
        return @"default";
    }
    NSString *t = [identifier stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return t.length ? t : @"default";
}

+ (NSPredicate *)hxcvd_predicateForUserIdentifier:(nullable NSString *)identifier {
    NSString *norm = [self hxcvd_normalizeUserIdentifier:identifier];
    if ([norm isEqualToString:@"default"]) {
        return [NSPredicate predicateWithFormat:@"(identifier == nil) OR (identifier == %@)", @"default"];
    }
    return [NSPredicate predicateWithFormat:@"identifier == %@", norm];
}

/// NSPersistentContainer.viewContext 使用主队列并发类型，仅允许在主线程访问。
- (void)hxdvd_performOnViewContext:(void (^)(void))block {
    if (!block) {
        return;
    }
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

+ (NSManagedObjectModel *)hxdvd_buildModel {
    NSManagedObjectModel *model = [[NSManagedObjectModel alloc] init];
    NSEntityDescription *entity = [[NSEntityDescription alloc] init];
    entity.name = kEntityName;
    entity.managedObjectClassName = @"NSManagedObject";

    NSMutableArray *props = [NSMutableArray array];

    NSAttributeDescription *taskId = [[NSAttributeDescription alloc] init];
    taskId.name = @"taskId";
    taskId.attributeType = NSStringAttributeType;
    taskId.optional = NO;
    [props addObject:taskId];

    NSAttributeDescription *userIdentifier = [[NSAttributeDescription alloc] init];
    userIdentifier.name = @"identifier";
    userIdentifier.attributeType = NSStringAttributeType;
    userIdentifier.optional = YES;
    [props addObject:userIdentifier];

    NSAttributeDescription *urlString = [[NSAttributeDescription alloc] init];
    urlString.name = @"urlString";
    urlString.attributeType = NSStringAttributeType;
    urlString.optional = NO;
    [props addObject:urlString];

    NSAttributeDescription *finalURLString = [[NSAttributeDescription alloc] init];
    finalURLString.name = @"finalURLString";
    finalURLString.attributeType = NSStringAttributeType;
    finalURLString.optional = YES;
    [props addObject:finalURLString];

    NSAttributeDescription *downloadType = [[NSAttributeDescription alloc] init];
    downloadType.name = @"downloadType";
    downloadType.attributeType = NSInteger16AttributeType;
    downloadType.optional = NO;
    downloadType.defaultValue = @(0);
    [props addObject:downloadType];

    NSAttributeDescription *state = [[NSAttributeDescription alloc] init];
    state.name = @"state";
    state.attributeType = NSInteger16AttributeType;
    state.optional = NO;
    state.defaultValue = @(HXCVDownloadStateWaiting);
    [props addObject:state];

    NSAttributeDescription *progress = [[NSAttributeDescription alloc] init];
    progress.name = @"progress";
    progress.attributeType = NSDoubleAttributeType;
    progress.optional = NO;
    progress.defaultValue = @(0.0);
    [props addObject:progress];

    NSAttributeDescription *bytesWritten = [[NSAttributeDescription alloc] init];
    bytesWritten.name = @"bytesWritten";
    bytesWritten.attributeType = NSInteger64AttributeType;
    bytesWritten.optional = NO;
    bytesWritten.defaultValue = @(0);
    [props addObject:bytesWritten];

    NSAttributeDescription *totalBytes = [[NSAttributeDescription alloc] init];
    totalBytes.name = @"totalBytes";
    totalBytes.attributeType = NSInteger64AttributeType;
    totalBytes.optional = NO;
    totalBytes.defaultValue = @(-1);
    [props addObject:totalBytes];

    NSAttributeDescription *localRelativePath = [[NSAttributeDescription alloc] init];
    localRelativePath.name = @"localRelativePath";
    localRelativePath.attributeType = NSStringAttributeType;
    localRelativePath.optional = YES;
    [props addObject:localRelativePath];

    NSAttributeDescription *resumeData = [[NSAttributeDescription alloc] init];
    resumeData.name = @"resumeData";
    resumeData.attributeType = NSBinaryDataAttributeType;
    resumeData.optional = YES;
    [props addObject:resumeData];

    NSAttributeDescription *errorMessage = [[NSAttributeDescription alloc] init];
    errorMessage.name = @"errorMessage";
    errorMessage.attributeType = NSStringAttributeType;
    errorMessage.optional = YES;
    [props addObject:errorMessage];

    NSAttributeDescription *createdAt = [[NSAttributeDescription alloc] init];
    createdAt.name = @"createdAt";
    createdAt.attributeType = NSDateAttributeType;
    createdAt.optional = NO;
    [props addObject:createdAt];

    NSAttributeDescription *updatedAt = [[NSAttributeDescription alloc] init];
    updatedAt.name = @"updatedAt";
    updatedAt.attributeType = NSDateAttributeType;
    updatedAt.optional = NO;
    [props addObject:updatedAt];

    entity.properties = props;

    NSEntityDescription *e = entity;
    model.entities = @[ e ];

    return model;
}

+ (instancetype)sharedController {
    static HXCVDPersistenceController *s;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        s = [[HXCVDPersistenceController alloc] init];
    });
    return s;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSManagedObjectModel *model = [HXCVDPersistenceController hxdvd_buildModel];
        NSPersistentContainer *container = [[NSPersistentContainer alloc] initWithName:@"HXCVD" managedObjectModel:model];
        _persistentContainer = container;
        [container.persistentStoreDescriptions enumerateObjectsUsingBlock:^(NSPersistentStoreDescription * _Nonnull obj, NSUInteger idx, BOOL * _Nonnull stop) {
            obj.shouldMigrateStoreAutomatically = YES;
            obj.shouldInferMappingModelAutomatically = YES;
        }];
        [container loadPersistentStoresWithCompletionHandler:^(NSPersistentStoreDescription * _Nonnull storeDescription, NSError * _Nullable error) {
            if (error) {
                NSLog(@"[HXCVD] Core Data 加载失败: %@", error);
            }
        }];
        container.viewContext.mergePolicy = NSMergeByPropertyObjectTrumpMergePolicy;
        container.viewContext.automaticallyMergesChangesFromParent = YES;
    }
    return self;
}

- (void)save:(NSError **)outError {
    __block NSError *localErr = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        if (!ctx.hasChanges) {
            return;
        }
        NSError *e = nil;
        [ctx save:&e];
        localErr = e;
    }];
    if (outError) {
        *outError = localErr;
    }
}

- (NSManagedObject *)hxdvd_fetchMOForTaskId:(NSString *)taskId inContext:(NSManagedObjectContext *)ctx {
    NSFetchRequest *req = [NSFetchRequest fetchRequestWithEntityName:kEntityName];
    req.fetchLimit = 1;
    req.predicate = [NSPredicate predicateWithFormat:@"taskId == %@", taskId];
    NSError *err = nil;
    NSArray *a = [ctx executeFetchRequest:req error:&err];
    if (err) {
        NSLog(@"[HXCVD] fetch 失败: %@", err);
        return nil;
    }
    return a.firstObject;
}

- (NSString *)insertTaskWithURLString:(NSString *)urlString downloadType:(HXCVDownloadType)type error:(NSError **)outError {
    return [self insertTaskWithURLString:urlString downloadType:type identifier:nil error:outError];
}

- (NSString *)insertTaskWithURLString:(NSString *)urlString downloadType:(HXCVDownloadType)type identifier:(NSString *)identifier error:(NSError **)outError {
    NSString *normId = [[self class] hxcvd_normalizeUserIdentifier:identifier];
    __block NSString *tid = nil;
    __block NSError *localErr = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSString *newTid = [[NSUUID UUID] UUIDString];
        NSDate *now = [NSDate date];
        NSManagedObject *mo = [NSEntityDescription insertNewObjectForEntityForName:kEntityName inManagedObjectContext:ctx];
        [mo setValue:newTid forKey:@"taskId"];
        [mo setValue:normId forKey:@"identifier"];
        [mo setValue:urlString forKey:@"urlString"];
        [mo setValue:nil forKey:@"finalURLString"];
        [mo setValue:@(type) forKey:@"downloadType"];
        [mo setValue:@(HXCVDownloadStateWaiting) forKey:@"state"];
        [mo setValue:@(0.0) forKey:@"progress"];
        [mo setValue:@0 forKey:@"bytesWritten"];
        [mo setValue:@(-1) forKey:@"totalBytes"];
        [mo setValue:nil forKey:@"localRelativePath"];
        [mo setValue:nil forKey:@"resumeData"];
        [mo setValue:nil forKey:@"errorMessage"];
        [mo setValue:now forKey:@"createdAt"];
        [mo setValue:now forKey:@"updatedAt"];
        NSError *e = nil;
        if ([ctx save:&e]) {
            tid = newTid;
        } else {
            localErr = e;
            [ctx rollback];
        }
    }];
    if (outError) {
        *outError = localErr;
    }
    return tid;
}

- (BOOL)updateTaskId:(NSString *)taskId
               state:(HXCVDownloadState)state
            progress:(double)progress
        bytesWritten:(int64_t)bytesWritten
          totalBytes:(int64_t)totalBytes
   localRelativePath:(NSString *)localRelativePath
        finalURLString:(NSString *)finalURLString
          errorMessage:(NSString *)errorMessage
               error:(NSError **)outError {
    __block BOOL ok = NO;
    __block NSError *localErr = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSManagedObject *mo = [self hxdvd_fetchMOForTaskId:taskId inContext:ctx];
        if (!mo) {
            return;
        }
        [mo setValue:@(state) forKey:@"state"];
        [mo setValue:@(progress) forKey:@"progress"];
        [mo setValue:@(bytesWritten) forKey:@"bytesWritten"];
        [mo setValue:@(totalBytes) forKey:@"totalBytes"];
        if (localRelativePath) {
            [mo setValue:localRelativePath forKey:@"localRelativePath"];
        }
        if (finalURLString) {
            [mo setValue:finalURLString forKey:@"finalURLString"];
        }
        if (errorMessage) {
            [mo setValue:errorMessage forKey:@"errorMessage"];
        }
        [mo setValue:[NSDate date] forKey:@"updatedAt"];
        NSError *e = nil;
        ok = [ctx save:&e];
        localErr = e;
    }];
    if (outError) {
        *outError = localErr;
    }
    return ok;
}

- (BOOL)setResumeData:(NSData *)resumeData forTaskId:(NSString *)taskId error:(NSError **)outError {
    __block BOOL ok = NO;
    __block NSError *localErr = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSManagedObject *mo = [self hxdvd_fetchMOForTaskId:taskId inContext:ctx];
        if (!mo) {
            return;
        }
        [mo setValue:resumeData forKey:@"resumeData"];
        [mo setValue:[NSDate date] forKey:@"updatedAt"];
        NSError *e = nil;
        ok = [ctx save:&e];
        localErr = e;
    }];
    if (outError) {
        *outError = localErr;
    }
    return ok;
}

- (NSData *)resumeDataForTaskId:(NSString *)taskId {
    __block NSData *data = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSManagedObject *mo = [self hxdvd_fetchMOForTaskId:taskId inContext:ctx];
        data = [mo valueForKey:@"resumeData"];
    }];
    return data;
}

- (BOOL)deleteTaskId:(NSString *)taskId error:(NSError **)outError {
    __block BOOL ok = YES;
    __block NSError *localErr = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSManagedObject *mo = [self hxdvd_fetchMOForTaskId:taskId inContext:ctx];
        if (!mo) {
            return;
        }
        [ctx deleteObject:mo];
        NSError *e = nil;
        ok = [ctx save:&e];
        localErr = e;
    }];
    if (outError) {
        *outError = localErr;
    }
    return ok;
}

- (HXCVDownloadItem *)hxdvd_itemFromMO:(NSManagedObject *)mo {
    NSString *taskId = [mo valueForKey:@"taskId"];
    NSString *idRaw = [mo valueForKey:@"identifier"];
    NSString *userId = [[self class] hxcvd_normalizeUserIdentifier:idRaw];
    NSString *urlString = [mo valueForKey:@"urlString"];
    NSString *finalURLString = [mo valueForKey:@"finalURLString"];
    HXCVDownloadType t = (HXCVDownloadType)[[mo valueForKey:@"downloadType"] shortValue];
    HXCVDownloadState s = (HXCVDownloadState)[[mo valueForKey:@"state"] shortValue];
    double progress = [[mo valueForKey:@"progress"] doubleValue];
    int64_t bw = [[mo valueForKey:@"bytesWritten"] longLongValue];
    int64_t tb = [[mo valueForKey:@"totalBytes"] longLongValue];
    NSString *localRel = [mo valueForKey:@"localRelativePath"];
    NSString *errMsg = [mo valueForKey:@"errorMessage"];
    NSDate *created = [mo valueForKey:@"createdAt"];
    NSDate *updated = [mo valueForKey:@"updatedAt"];
    return [HXCVDownloadItem hxdvd_itemWithTaskId:taskId
                                       identifier:userId
                                        urlString:urlString
                                   finalURLString:finalURLString
                                     downloadType:t
                                            state:s
                                         progress:progress
                                     bytesWritten:bw
                                       totalBytes:tb
                                localRelativePath:localRel
                                     errorMessage:errMsg
                                        createdAt:created
                                        updatedAt:updated];
}

- (nullable HXCVDownloadItem *)fetchTaskWithId:(NSString *)taskId {
    __block HXCVDownloadItem *item = nil;
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSManagedObject *mo = [self hxdvd_fetchMOForTaskId:taskId inContext:ctx];
        if (mo) {
            item = [self hxdvd_itemFromMO:mo];
        }
    }];
    return item;
}

- (NSArray<HXCVDownloadItem *> *)fetchAllTasksSortedByUpdatedDesc {
    __block NSArray<HXCVDownloadItem *> *result = @[];
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSFetchRequest *req = [NSFetchRequest fetchRequestWithEntityName:kEntityName];
        NSSortDescriptor *sd = [NSSortDescriptor sortDescriptorWithKey:@"updatedAt" ascending:NO];
        req.sortDescriptors = @[ sd ];
        NSError *err = nil;
        NSArray *rows = [ctx executeFetchRequest:req error:&err];
        if (err) {
            NSLog(@"[HXCVD] fetchAll 失败: %@", err);
            result = @[];
            return;
        }
        NSMutableArray *out = [NSMutableArray arrayWithCapacity:rows.count];
        for (NSManagedObject *mo in rows) {
            [out addObject:[self hxdvd_itemFromMO:mo]];
        }
        result = [out copy];
    }];
    return result;
}

- (NSArray<HXCVDownloadItem *> *)fetchAllTasksSortedByUpdatedDescForIdentifier:(NSString *)identifier {
    __block NSArray<HXCVDownloadItem *> *result = @[];
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSFetchRequest *req = [NSFetchRequest fetchRequestWithEntityName:kEntityName];
        req.predicate = [[self class] hxcvd_predicateForUserIdentifier:identifier];
        NSSortDescriptor *sd = [NSSortDescriptor sortDescriptorWithKey:@"updatedAt" ascending:NO];
        req.sortDescriptors = @[ sd ];
        NSError *err = nil;
        NSArray *rows = [ctx executeFetchRequest:req error:&err];
        if (err) {
            NSLog(@"[HXCVD] fetchAll(identifier) 失败: %@", err);
            result = @[];
            return;
        }
        NSMutableArray *out = [NSMutableArray arrayWithCapacity:rows.count];
        for (NSManagedObject *mo in rows) {
            [out addObject:[self hxdvd_itemFromMO:mo]];
        }
        result = [out copy];
    }];
    return result;
}

- (NSArray<HXCVDownloadItem *> *)fetchTasksWithState:(HXCVDownloadState)state {
    __block NSArray<HXCVDownloadItem *> *result = @[];
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSFetchRequest *req = [NSFetchRequest fetchRequestWithEntityName:kEntityName];
        req.predicate = [NSPredicate predicateWithFormat:@"state == %@", @((short)state)];
        NSSortDescriptor *sd = [NSSortDescriptor sortDescriptorWithKey:@"updatedAt" ascending:NO];
        req.sortDescriptors = @[ sd ];
        NSError *err = nil;
        NSArray *rows = [ctx executeFetchRequest:req error:&err];
        if (err) {
            NSLog(@"[HXCVD] fetchTasksWithState 失败: %@", err);
            result = @[];
            return;
        }
        NSMutableArray *out = [NSMutableArray arrayWithCapacity:rows.count];
        for (NSManagedObject *mo in rows) {
            [out addObject:[self hxdvd_itemFromMO:mo]];
        }
        result = [out copy];
    }];
    return result;
}

- (NSArray<HXCVDownloadItem *> *)fetchTasksWithState:(HXCVDownloadState)state identifier:(NSString *)identifier {
    __block NSArray<HXCVDownloadItem *> *result = @[];
    [self hxdvd_performOnViewContext:^{
        NSManagedObjectContext *ctx = self.persistentContainer.viewContext;
        NSFetchRequest *req = [NSFetchRequest fetchRequestWithEntityName:kEntityName];
        NSPredicate *statePred = [NSPredicate predicateWithFormat:@"state == %@", @((short)state)];
        NSPredicate *idPred = [[self class] hxcvd_predicateForUserIdentifier:identifier];
        req.predicate = [NSCompoundPredicate andPredicateWithSubpredicates:@[ statePred, idPred ]];
        NSSortDescriptor *sd = [NSSortDescriptor sortDescriptorWithKey:@"updatedAt" ascending:NO];
        req.sortDescriptors = @[ sd ];
        NSError *err = nil;
        NSArray *rows = [ctx executeFetchRequest:req error:&err];
        if (err) {
            NSLog(@"[HXCVD] fetchTasksWithState(identifier) 失败: %@", err);
            result = @[];
            return;
        }
        NSMutableArray *out = [NSMutableArray arrayWithCapacity:rows.count];
        for (NSManagedObject *mo in rows) {
            [out addObject:[self hxdvd_itemFromMO:mo]];
        }
        result = [out copy];
    }];
    return result;
}

@end
