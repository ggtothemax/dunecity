#include <misc/DesktopUpdater.h>
#import <Sparkle/Sparkle.h>

@interface DuneUpdateDelegate : NSObject <SPUUpdaterDelegate>
@property(nonatomic, copy) NSString* failure;
@end
@implementation DuneUpdateDelegate
- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error {
    if ([error.domain isEqualToString:SUSparkleErrorDomain] &&
        (error.code == SUInstallationCanceledError || error.code == SUNoUpdateError)) return;
    self.failure = error.localizedDescription;
}
@end

namespace {
SPUStandardUpdaterController* controller;
DuneUpdateDelegate* delegate;
}
namespace NativeUpdater {
bool begin(std::string& error) {
    @autoreleasepool {
        if (!controller) {
            delegate = [DuneUpdateDelegate new];
            controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:delegate userDriverDelegate:nil];
            NSError* startError = nil;
            if (![controller.updater startUpdater:&startError]) {
                error = startError.localizedDescription.UTF8String;
                controller = nil; delegate = nil; return false;
            }
            controller.updater.automaticallyChecksForUpdates = NO;
            controller.updater.automaticallyDownloadsUpdates = NO;
        }
        delegate.failure = nil;
        if (!controller.updater.canCheckForUpdates) { error = "An update is already in progress."; return false; }
        [controller checkForUpdates:nil];
        return true;
    }
}
bool busy() { return controller && controller.updater.sessionInProgress; }
std::string takeError() {
    @autoreleasepool {
        const std::string error = delegate.failure ? delegate.failure.UTF8String : "";
        delegate.failure = nil; return error;
    }
}
void cleanup() { /* Sparkle owns its installer handoff during application exit. */ }
}
