#include "MicPermission.h"

#if JUCE_MAC
 #import <AVFoundation/AVFoundation.h>
 #include <juce_events/juce_events.h>
#endif

namespace livemix
{
namespace MicPermission
{

#if JUCE_MAC

State check()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:    return State::Granted;
        case AVAuthorizationStatusNotDetermined: return State::Undetermined;
        case AVAuthorizationStatusDenied:        return State::Denied;
        case AVAuthorizationStatusRestricted:    return State::Restricted;
        default:                                 return State::Undetermined;
    }
}

void request (std::function<void (bool)> done)
{
    if (check() != State::Undetermined) { if (done) done (check() == State::Granted); return; }
    // The answer arrives on a private queue; everything DLIVE does with it belongs on the
    // message thread, so it is handed back there.
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                             completionHandler: ^(BOOL granted)
    {
        if (! done) return;
        juce::MessageManager::callAsync ([done, granted] { done (granted == YES); });
    }];
}

#else

State check() { return State::Granted; }
void request (std::function<void (bool)> done) { if (done) done (true); }

#endif

bool denied()
{
    const auto s = check();
    return s == State::Denied || s == State::Restricted;
}

} // namespace MicPermission
} // namespace livemix
