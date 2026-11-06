#pragma once
#include <functional>

namespace el {
// Asks macOS for microphone (audio input) access once, before any audio device is opened, so
// CoreAudio doesn't re-trigger the system prompt every time the input device is reopened.
// The callback runs on the message thread with whether input access is granted.
// On other platforms it calls back immediately with true.
void requestMicrophoneAccess (std::function<void (bool granted)> callback);
}
