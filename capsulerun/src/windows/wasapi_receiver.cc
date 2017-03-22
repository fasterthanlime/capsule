
// this file is pretty much textbook msdn:
// https://msdn.microsoft.com/en-us/library/windows/desktop/dd370800(v=vs.85).aspx 

#include "wasapi_receiver.h"

// PKEY_Device_FriendlyName
#include <Functiondiscoverykeys_devpkey.h>

// TODO: signal errors without exceptions
#include <stdexcept>

#include <microprofile.h>

MICROPROFILE_DEFINE(WasapiReceiveFrames, "Wasapi", "WasapiReceiveFrames", 0xff00ff00);

namespace capsule {
namespace audio {

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

/**
 * Release a COM resource, if it's non-null
 */
static inline void SafeRelease(IUnknown *p) {
  if (p) {
    p->Release();
  }
}

WasapiReceiver::WasapiReceiver() {
  memset(&afmt_, 0, sizeof(*afmt));

  HRESULT hr;

  hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    throw runtime_error("CoInitializeEx failed");
  }

  hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&enumerator_);
  if (FAILED(hr)) {
    throw runtime_error("Could not create device enumerator instance");
  }

  hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
  if (FAILED(hr)) {
    throw runtime_error("Could not get default audio endpoint");
  }

  IPropertyStore *props = nullptr;
  hr = device->OpenPropertyStore(STGM_READ, &props);
  if (FAILED(hr)) {
    throw runtime_error("Could not get open endpoint property store");
  }

  PROPVARIANT var_name;
  // Initialize container for property value.
  PropVariantInit(&var_name);

  // Get the endpoint's friendly-name property.
  hr = props->GetValue(PKEY_Device_FriendlyName, &var_name);
  if (FAILED(hr)) {
    throw runtime_error("Could not get device friendly name");
  }

  // Print endpoint friendly name and endpoint ID.
  CapsuleLog("WasapiReceiver: Capturing from: \"%S\"", var_name.pwszVal);

  PropVariantClear(&var_name);
  SafeRelease(props);

  hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**) &audio_client_);
  if (FAILED(hr)) {
    throw runtime_error("Could not activate audio device");
  }

  hr = audio_client->GetMixFormat(&pwfx_);
  if (FAILED(hr)) {
    throw runtime_error("Could not get mix format");
  }

  afmt_.channels = pwfx_->nChannels;
  afmt_.samplerate = pwfx_->nSamplesPerSec;
  afmt_.samplewidth = pwfx_->wBitsPerSample;

  REFERENCE_TIME hns_requested_duration = kReftimesPerSec * 4;
  hr = audio_client_->Initialize(
      AUDCLNT_SHAREMODE_SHARED, // we don't need exclusive access
      AUDCLNT_STREAMFLAGS_LOOPBACK, // we want to capture output, not an input
      hns_requested_duration,
      0,
      pwfx_,
      NULL);
  if (FAILED(hr)) {
    throw runtime_error("Could not initialize audio client");
  }

  UINT32 buffer_frame_count;

  // Get the size of the allocated buffer.
  hr = audio_client_->GetBufferSize(&buffer_frame_count);
  if (hr != S_OK) {
    throw runtime_error("Could not get buffer size");
  }

  CapsuleLog("WasapiReceiver: Buffer frame count: %d", buffer_frame_count);

  hr = audio_client_->GetService(
      IID_IAudioCaptureClient,
      (void **) &capture_client_);
  if (FAILED(hr)) {
    throw runtime_error("Could not get capture client");
  }

  hr = audio_client_->Start();
  if (hr != S_OK) {
    printf("Could not start capture");
    exit(1);
  }
}

int WasapiReceiver::ReceiveFormat(audio_format_t *afmt) {
  memcpy(afmt, &afmt_, sizeof(*afmt));
  return 0;
}

void *WasapiReceiver::ReceiveFrames(int *frames_received) {
  MICROPROFILE_SCOPE(WasapiReceiveFrames);

  std::lock_guard<std::mutex> lock(stopped_mutex_);

  if (stopped) {
    *frames_received = 0;
    return nullptr;
  }

  BYTE *buffer;
  DWORD flags;
  HRESULT hr;
  UINT32 num_frames_available;

  if (num_frames_received > 0) {
    hr = capture_client_->ReleaseBuffer(num_frames_received);
    if (FAILED(hr)) {
      CapsuleLog("WasapiReceiver: Could not release buffer: error %d (%x)\n", hr, hr);
      stopped = true;
      *frames_received = 0;
      return nullptr;
    }
  }

  hr = capture_client_->GetBuffer(
    &buffer,
    &num_frames_available,
    &flags,
    NULL,
    NULL
  );

  if (FAILED(hr)) {
    if (hr == AUDCLNT_S_BUFFER_EMPTY) {
      // good!
      *frames_received = 0;
      num_frames_received_ = 0;
      return nullptr;
    } else {
      CapsuleLog("WasapiReceiver: Could not get buffer: error %d (%x)\n", hr, hr);
      stopped_ = true;
      *frames_received = 0;
      return nullptr;
    }
  }

  *frames_received = num_frames_available;
  num_frames_received_ = num_frames_available;
  return buffer;
}

void WasapiReceiver::Stop() {
  std::lock_guard<std::mutex> lock(stopped_mutex_);

  HRESULT hr = audio_client_->Stop();
  if (FAILED(hr)) {
    CapsuleLog("WasapiReceiver: Could not stop audio client: error %d (%x)", hr, hr);
  }
  stopped_ = true;
}

WasapiReceiver::~WasapiReceiver() {
  CoTaskMemFree(pwfx);
  SafeRelease(enumerator_)
  SafeRelease(device_)
  SafeRelease(audio_client_)
  SafeRelease(capture_client_)
}

} // namespace audio
} // namespace capsule