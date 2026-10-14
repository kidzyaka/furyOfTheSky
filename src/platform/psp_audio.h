#pragma once

namespace platform {

// Reserves one hardware stereo channel and starts the audio thread that pulls
// audio::mixer() in 1024-frame blocks (docs/decisions/0024). Returns false if
// the channel or thread could not be created: the game then runs silent.
bool initAudio();

// Stops the thread and releases the channel (before exiting to the XMB).
void shutdownAudio();

}  // namespace platform
