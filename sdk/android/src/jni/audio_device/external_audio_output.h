/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_EXTERNAL_AUDIO_OUTPUT_H_
#define SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_EXTERNAL_AUDIO_OUTPUT_H_

#include <jni.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "api/audio/audio_device_defines.h"
#include "api/sequence_checker.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "sdk/android/src/jni/audio_device/audio_device_module.h"
#include "third_party/jni_zero/jni_zero.h"

namespace webrtc {
namespace jni {

/**
 * Callback interface for Java layer notifications.
 * This is used to avoid including generated JNI headers in this class.
 */
class ExternalAudioOutputCallback {
 public:
  virtual ~ExternalAudioOutputCallback() = default;
  virtual void OnPlayoutStarted() = 0;
  virtual void OnPlayoutStopped() = 0;
};

/**
 * External audio output that delivers audio data to Java layer via JNI callbacks.
 * 
 * This replaces AudioTrackJni for scenarios where APP layer controls audio playback.
 * Instead of using Android's AudioTrack directly, this class delivers decoded audio
 * to Java via the ExternalAudioDeviceModule.AudioBridge.onPlaybackData() callback.
 * 
 * Thread model:
 * - Construction/destruction on signaling thread
 * - Init/Start/Stop on WebRTC worker thread
 * - Playout is pulled by Java/Core via JNI (no internal playout thread)
 */
class ExternalAudioOutput : public AudioOutput {
 public:
  ExternalAudioOutput(JNIEnv* env,
                      const AudioParameters& audio_parameters,
                      ExternalAudioOutputCallback* callback);
  ~ExternalAudioOutput() override;

  // AudioOutput interface implementation
  int32_t Init() override;
  int32_t Terminate() override;

  int32_t InitPlayout() override;
  bool PlayoutIsInitialized() const override;

  int32_t StartPlayout() override;
  int32_t StopPlayout() override;
  bool Playing() const override;

  // Volume control - delegated to APP layer
  bool SpeakerVolumeIsAvailable() override { return false; }
  int SetSpeakerVolume(uint32_t volume) override { return -1; }
  std::optional<uint32_t> SpeakerVolume() const override { return std::nullopt; }
  std::optional<uint32_t> MaxSpeakerVolume() const override { return std::nullopt; }
  std::optional<uint32_t> MinSpeakerVolume() const override { return std::nullopt; }

  void AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) override;

  int GetPlayoutUnderrunCount() override { return playout_underrun_count_; }

  int PullPlayoutData(void* audio_data, size_t num_samples);

  std::optional<AudioDeviceModule::Stats> GetStats() const override {
    return std::nullopt;
  }

 private:
  // Callback for Java layer notifications
  ExternalAudioOutputCallback* callback_;
  
  // Audio parameters
  const AudioParameters audio_parameters_;
  
  // Audio device buffer for getting data from WebRTC
  AudioDeviceBuffer* audio_device_buffer_ = nullptr;
  
  // State flags
  bool initialized_ = false;
  bool playing_ = false;
  
  // Number of audio frames per buffer
  size_t frames_per_buffer_ = 0;
  
  // Underrun counter
  int playout_underrun_count_ = 0;
  
  // Thread checker for WebRTC thread
  SequenceChecker thread_checker_;
};

}  // namespace jni
}  // namespace webrtc

#endif  // SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_EXTERNAL_AUDIO_OUTPUT_H_
