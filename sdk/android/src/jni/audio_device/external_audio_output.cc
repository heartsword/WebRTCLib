/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/audio_device/external_audio_output.h"

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace jni {

ExternalAudioOutput::ExternalAudioOutput(
    JNIEnv* env,
    const AudioParameters& audio_parameters,
    ExternalAudioOutputCallback* callback)
    : callback_(callback),
      audio_parameters_(audio_parameters) {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::ctor";
  RTC_LOG(LS_INFO) << "Audio parameters: sample_rate=" << audio_parameters_.sample_rate()
                   << ", channels=" << audio_parameters_.channels()
                   << ", frames_per_buffer=" << audio_parameters_.frames_per_buffer();
  
  // Calculate frames per buffer (10ms of audio)
  frames_per_buffer_ = audio_parameters_.sample_rate() / 100;
}

ExternalAudioOutput::~ExternalAudioOutput() {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::dtor";
  RTC_DCHECK(thread_checker_.IsCurrent());
  Terminate();
}

int32_t ExternalAudioOutput::Init() {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::Init";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  if (initialized_) {
    return 0;
  }

  initialized_ = true;
  RTC_LOG(LS_INFO) << "ExternalAudioOutput initialized";
  return 0;
}

int32_t ExternalAudioOutput::Terminate() {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::Terminate";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  if (!initialized_) {
    return 0;
  }
  
  StopPlayout();

  initialized_ = false;
  return 0;
}

int32_t ExternalAudioOutput::InitPlayout() {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::InitPlayout";
  RTC_DCHECK(thread_checker_.IsCurrent());
  RTC_DCHECK(initialized_);
  RTC_DCHECK(!playing_);
  
  // Configure audio device buffer with our parameters
  if (audio_device_buffer_ != nullptr) {
    audio_device_buffer_->SetPlayoutSampleRate(audio_parameters_.sample_rate());
    audio_device_buffer_->SetPlayoutChannels(audio_parameters_.channels());
  }
  
  return 0;
}

bool ExternalAudioOutput::PlayoutIsInitialized() const {
  return initialized_;
}

int32_t ExternalAudioOutput::StartPlayout() {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::StartPlayout";
  RTC_DCHECK(thread_checker_.IsCurrent());
  RTC_DCHECK(initialized_);
  
  if (playing_) {
    return 0;
  }
  
  // Notify via callback
  if (callback_) {
    callback_->OnPlayoutStarted();
  }
  
  playing_ = true;
  RTC_LOG(LS_INFO) << "Playout started";
  return 0;
}

int32_t ExternalAudioOutput::StopPlayout() {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::StopPlayout";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  if (!playing_) {
    return 0;
  }

  // Notify via callback
  if (callback_) {
    callback_->OnPlayoutStopped();
  }
  
  playing_ = false;
  RTC_LOG(LS_INFO) << "Playout stopped";
  return 0;
}

bool ExternalAudioOutput::Playing() const {
  return playing_;
}

void ExternalAudioOutput::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {
  RTC_LOG(LS_INFO) << "ExternalAudioOutput::AttachAudioBuffer";
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  audio_device_buffer_ = audioBuffer;
  
  // Configure the audio buffer with our parameters
  if (audio_device_buffer_ != nullptr) {
    audio_device_buffer_->SetPlayoutSampleRate(audio_parameters_.sample_rate());
    audio_device_buffer_->SetPlayoutChannels(audio_parameters_.channels());
  }
}

int ExternalAudioOutput::PullPlayoutData(void* audio_data, size_t num_samples) {
  if (!playing_ || !initialized_ || audio_device_buffer_ == nullptr ||
      audio_data == nullptr) {
    return 0;
  }

  const int32_t samples =
      audio_device_buffer_->RequestPlayoutData(static_cast<int>(num_samples));
  if (samples <= 0) {
    playout_underrun_count_++;
    return 0;
  }

  const size_t num_bytes =
      audio_device_buffer_->GetPlayoutData(reinterpret_cast<int16_t*>(audio_data));
  if (num_bytes == 0) {
    playout_underrun_count_++;
    return 0;
  }

  return samples;
}

}  // namespace jni
}  // namespace webrtc
