/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_EXTERNAL_AUDIO_INPUT_H_
#define SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_EXTERNAL_AUDIO_INPUT_H_

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "api/audio/audio_device_defines.h"
#include "api/sequence_checker.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "sdk/android/src/jni/audio_device/audio_device_module.h"
#include "sdk/android/native_api/jni/scoped_java_ref.h"

namespace webrtc {
namespace jni {

/**
 * Callback interface for Java layer notifications.
 * This is used to avoid including generated JNI headers in this class.
 * 
 * Recording supports two modes:
 * 1. PUSH mode: Java calls OnRecordedData to push audio to native
 * 2. PULL mode: Native calls OnRecordData to pull audio from Java
 */
class ExternalAudioInputCallback {
 public:
  virtual ~ExternalAudioInputCallback() = default;
  virtual void OnRecordingStarted() = 0;
  virtual void OnRecordingStopped() = 0;
};

/**
 * External audio input that receives audio data from Java layer via JNI.
 * 
 * This replaces AudioRecordJni for scenarios where APP layer controls audio capture.
 * 
 * Thread model:
 * - Construction/destruction on signaling thread
 * - Init/Start/Stop on WebRTC worker thread  
 * - PUSH mode: OnRecordedData from APP layer's audio capture thread
 */
class ExternalAudioInput : public AudioInput {
 public:
  ExternalAudioInput(JNIEnv* env,
                     const AudioParameters& audio_parameters,
                     ExternalAudioInputCallback* callback);
  ~ExternalAudioInput() override;

  // AudioInput interface implementation
  int32_t Init() override;
  int32_t Terminate() override;

  int32_t InitRecording() override;
  bool RecordingIsInitialized() const override;

  int32_t StartRecording() override;
  int32_t StopRecording() override;
  bool Recording() const override;

  void AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) override;

  // External audio doesn't support built-in AEC/NS
  bool IsAcousticEchoCancelerSupported() const override { return false; }
  bool IsNoiseSuppressorSupported() const override { return false; }
  int32_t EnableBuiltInAEC(bool enable) override { return -1; }
  int32_t EnableBuiltInNS(bool enable) override { return -1; }

  /**
   * PUSH mode: Called from Java via JNI when audio data is available.
   * This is invoked on the APP layer's audio capture thread.
   * 
   * @param env JNI environment
   * @param byte_buffer Direct ByteBuffer containing PCM audio data
   * @param length Number of bytes in buffer
   * @param capture_timestamp_ns Capture timestamp in nanoseconds
   */
  void OnRecordedData(JNIEnv* env,
                      const JavaParamRef<jobject>& byte_buffer,
                      int length,
                      int64_t capture_timestamp_ns);

 private:
  // Callback for Java layer notifications
  ExternalAudioInputCallback* callback_;
  
  // Audio parameters
  const AudioParameters audio_parameters_;
  
  // Audio device buffer for delivering data to WebRTC
  AudioDeviceBuffer* audio_device_buffer_ = nullptr;
  
  // State flags
  bool initialized_ = false;
  bool recording_ = false;
  
  // Number of audio frames per buffer (samples per channel)
  size_t frames_per_buffer_ = 0;
  
  // Thread checker for WebRTC thread
  SequenceChecker thread_checker_;
  
  // Thread checker for Java callback thread (detached in constructor)
  SequenceChecker thread_checker_java_;
};

}  // namespace jni
}  // namespace webrtc

#endif  // SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_EXTERNAL_AUDIO_INPUT_H_
