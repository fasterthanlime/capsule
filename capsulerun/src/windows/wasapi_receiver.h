#pragma once

#include "../shared/audio_receiver.h"

// IMMDeviceEnumerator, IMMDevice
#include <mmDeviceapi.h>
// IAudioClient, IAudioCaptureClient
#include <audioclient.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#include <mutex>

namespace capsule {
namespace audio {

// REFERENCE_TIME time units per second and per millisecond
const long long kReftimesPerSec = 10000000LL;
const long long kReftimesPerMillisec = 10000LL;

class WasapiReceiver : public AudioReceiver {
  public:
    WasapiReceiver();
    virtual ~WasapiReceiver();

    virtual int ReceiveFormat(audio_format_t *afmt);
    virtual void *ReceiveFrames(int *frames_received);
    virtual void Stop();

  private:
    audio_format_t afmt_;
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device_ = nullptr;
    IAudioClient *audio_client_ = nullptr;
    IAudioCaptureClient *capture_client_ = nullptr;
    WAVEFORMATEX *pwfx_ = nullptr;
    int num_frames_received_ = 0;

    bool stopped_ = false;    
    std::mutex stopped_mutex_;
};

} // namespace capsule
} // namespace audio