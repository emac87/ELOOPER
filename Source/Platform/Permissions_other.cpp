#include "Permissions.h"
#if ! defined (__APPLE__)
namespace el {
void requestMicrophoneAccess (std::function<void (bool)> callback) { callback (true); }
}
#endif
