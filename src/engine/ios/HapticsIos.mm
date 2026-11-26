// iPhone haptics through UIKit's feedback generators (SDL has no iPhone vibration). Built on iOS only,
// with ARC. UIKit wants these on the main thread; SDL runs spindle's loop there, and anything else is
// sent to the main queue.

#import <UIKit/UIKit.h>

#include "engine/Haptics.hpp"

namespace {

UIImpactFeedbackGenerator* gLight = nil;
UIImpactFeedbackGenerator* gRigid = nil;
UIImpactFeedbackGenerator* gHeavy = nil;
UIImpactFeedbackGenerator* gSoft = nil;
UINotificationFeedbackGenerator* gNotify = nil;
UISelectionFeedbackGenerator* gSelect = nil;

void onMain(void (^block)(void)) {
    if ([NSThread isMainThread])
        block();
    else
        dispatch_async(dispatch_get_main_queue(), block);
}

} // namespace

namespace spindle::ios {

void hapticPrepare() {
    onMain(^{
      gLight = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
      gRigid = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleRigid];
      gHeavy = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleHeavy];
      gSoft = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleSoft];
      gNotify = [[UINotificationFeedbackGenerator alloc] init];
      gSelect = [[UISelectionFeedbackGenerator alloc] init];
      [gLight prepare];
      [gRigid prepare];
      [gHeavy prepare];
    });
}

void haptic(arcade::HapticKind kind, float strength) {
    const CGFloat s = static_cast<CGFloat>(strength < 0.05f ? 0.05f : (strength > 1.0f ? 1.0f : strength));
    onMain(^{
      switch (kind) {
      case arcade::HapticKind::Tick:
      case arcade::HapticKind::Letter: [gLight impactOccurredWithIntensity:s]; break;
      case arcade::HapticKind::CloseCall: [gRigid impactOccurredWithIntensity:s]; break;
      case arcade::HapticKind::Fail:
          [gHeavy impactOccurredWithIntensity:s];
          [gNotify notificationOccurred:UINotificationFeedbackTypeError];
          break;
      case arcade::HapticKind::Fire:
      case arcade::HapticKind::Clear: [gNotify notificationOccurred:UINotificationFeedbackTypeSuccess]; break;
      case arcade::HapticKind::Digit: [gSoft impactOccurredWithIntensity:s]; break;
      case arcade::HapticKind::Crown: [gHeavy impactOccurredWithIntensity:s]; break;
      case arcade::HapticKind::Select: [gSelect selectionChanged]; break;
      }
    });
}

} // namespace spindle::ios
