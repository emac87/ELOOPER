#include "Permissions.h"
#include <JuceHeader.h>
#import <AVFoundation/AVFoundation.h>

namespace el {

void requestMicrophoneAccess (std::function<void (bool)> callback)
{
    if (@available (macOS 10.14, *)) {
        const auto status = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];
        if (status == AVAuthorizationStatusAuthorized) { callback (true); return; }
        if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) { callback (false); return; }
        auto cb = std::make_shared<std::function<void (bool)>> (std::move (callback));
        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio completionHandler: ^(BOOL granted) {
            const bool g = granted == YES;
            juce::MessageManager::callAsync ([cb, g] { (*cb) (g); });
        }];
        return;
    }
    callback (true);
}

} // namespace el
