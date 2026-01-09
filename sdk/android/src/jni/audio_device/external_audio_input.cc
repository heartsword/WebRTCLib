/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/audio_device/external_audio_input.h"

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace jni {

ExternalAudioInput::ExternalAudioInput(
    JNIEnv* env,
    const AudioParameters& audio_parameters,
    ExternalAudioInputCallback* callback)
    : callback_(callback),
      audio_parameters_(audio_parameters) {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::ctor";
  RTC_LOG(LS_INFO) << "Audio parameters: sample_rate=" << audio_parameters_.sample_rate()
                   << ", channels=" << audio_parameters_.channels()
                   << ", frames_per_buffer=" << audio_parameters_.frames_per_buffer();
  
  // Calculate frames per buffer (10ms of audio)
  frames_per_buffer_ = audio_parameters_.sample_rate() / 100;
  
  // Detach thread checker for Java callback thread
  thread_checker_java_.Detach();
}

ExternalAudioInput::~ExternalAudioInput() {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::dtor";
  RTC_DCHECK(thread_checker_.IsCurrent());
  Terminate();
}

int32_t ExternalAudioInput::Init() {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::Init";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  if (initialized_) {
    return 0;
  }

  initialized_ = true;
  RTC_LOG(LS_INFO) << "ExternalAudioInput initialized";
  return 0;
}

int32_t ExternalAudioInput::Terminate() {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::Terminate";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  if (!initialized_) {
    return 0;
  }
  
  StopRecording();

  initialized_ = false;
  return 0;
}

int32_t ExternalAudioInput::InitRecording() {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::InitRecording";
  RTC_DCHECK(thread_checker_.IsCurrent());
  RTC_DCHECK(initialized_);
  RTC_DCHECK(!recording_);
  
  // Configure audio device buffer with our parameters
  if (audio_device_buffer_ != nullptr) {
    audio_device_buffer_->SetRecordingSampleRate(audio_parameters_.sample_rate());
    audio_device_buffer_->SetRecordingChannels(audio_parameters_.channels());
  }
  
  return 0;
}

bool ExternalAudioInput::RecordingIsInitialized() const {
  return initialized_;
}

int32_t ExternalAudioInput::StartRecording() {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::StartRecording";
  RTC_DCHECK(thread_checker_.IsCurrent());
  RTC_DCHECK(initialized_);
  
  if (recording_) {
    return 0;
  }
  
  // Notify Java that recording is starting
  if (callback_) {
    callback_->OnRecordingStarted();
  }

  recording_ = true;
  RTC_LOG(LS_INFO) << "Recording started (PUSH mode)";
  return 0;
}

int32_t ExternalAudioInput::StopRecording() {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::StopRecording";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  if (!recording_) {
    return 0;
  }

  // Notify Java that recording has stopped
  if (callback_) {
    callback_->OnRecordingStopped();
  }
  
  recording_ = false;
  RTC_LOG(LS_INFO) << "Recording stopped";
  return 0;
}

bool ExternalAudioInput::Recording() const {
  return recording_;
}

void ExternalAudioInput::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {
  RTC_LOG(LS_INFO) << "ExternalAudioInput::AttachAudioBuffer";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  audio_device_buffer_ = audioBuffer;
  
  // Configure the audio buffer with our parameters
  if (audio_device_buffer_ != nullptr) {
    audio_device_buffer_->SetRecordingSampleRate(audio_parameters_.sample_rate());
    audio_device_buffer_->SetRecordingChannels(audio_parameters_.channels());
  }
}

void ExternalAudioInput::OnRecordedData(
    JNIEnv* env,
    const JavaParamRef<jobject>& byte_buffer,
    int length,
    int64_t capture_timestamp_ns) {
  RTC_DCHECK(thread_checker_java_.IsCurrent());

  if (!recording_) {
    RTC_LOG(LS_WARNING) << "OnRecordedData called while not recording";
    return;
  }
  
  if (audio_device_buffer_ == nullptr) {
    RTC_LOG(LS_WARNING) << "OnRecordedData: audio_device_buffer_ is null";
    return;
  }
  
  // Get direct buffer address
  void* buffer_address = env->GetDirectBufferAddress(byte_buffer.obj());
  if (buffer_address == nullptr) {
    RTC_LOG(LS_ERROR) << "OnRecordedData: GetDirectBufferAddress returned null";
    return;
  }
  
  // Calculate number of samples
  const size_t bytes_per_sample = 2;  // 16-bit PCM
  const size_t num_channels = audio_parameters_.channels();
  const size_t num_samples = length / (bytes_per_sample * num_channels);
  
  // Set recorded buffer and deliver to WebRTC
  audio_device_buffer_->SetRecordedBuffer(buffer_address, num_samples);
  
  // Convert nanoseconds to milliseconds for VQE timestamp
  // Note: WebRTC expects timestamp for VQE processing
  audio_device_buffer_->SetVQEData(0, 0);
  
  // Deliver the recorded data
  audio_device_buffer_->DeliverRecordedData();
}

}  // namespace jni
}  // namespace webrtc
