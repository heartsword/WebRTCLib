/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <jni.h>

#include <memory>
#include <utility>

#include "api/audio/audio_device_defines.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "sdk/android/src/jni/audio_device/audio_device_module.h"
#include "sdk/android/src/jni/audio_device/external_audio_input.h"
#include "sdk/android/src/jni/audio_device/external_audio_output.h"
#include "sdk/android/generated_external_audio_jni/ExternalAudioDeviceModule_jni.h"
#include "sdk/android/native_api/jni/java_types.h"
#include "sdk/android/native_api/jni/scoped_java_ref.h"

namespace webrtc {
namespace jni {

namespace {

/**
 * Implementation of ExternalAudioInputCallback that calls Java methods.
 * 
 * This class bridges the C++ callback interface to Java method calls.
 * It holds a global reference to the Java ExternalAudioDeviceModule object.
 */
class AudioInputCallbackImpl : public ExternalAudioInputCallback {
 public:
  AudioInputCallbackImpl(JNIEnv* env, const JavaParamRef<jobject>& j_module,
                         int /* sample_rate */, int /* channels */)
      : j_module_(env, j_module) {
    RTC_LOG(LS_INFO) << "AudioInputCallbackImpl::ctor";
  }
  
  ~AudioInputCallbackImpl() override {
    RTC_LOG(LS_INFO) << "AudioInputCallbackImpl::dtor";
  }
  
  void OnRecordingStarted() override {
    RTC_LOG(LS_INFO) << "AudioInputCallbackImpl::OnRecordingStarted";
    JNIEnv* env = jni_zero::AttachCurrentThread();
    Java_ExternalAudioDeviceModule_onRecordingStartedFromNative(env, j_module_);
  }
  
  void OnRecordingStopped() override {
    RTC_LOG(LS_INFO) << "AudioInputCallbackImpl::OnRecordingStopped";
    JNIEnv* env = jni_zero::AttachCurrentThread();
    Java_ExternalAudioDeviceModule_onRecordingStoppedFromNative(env, j_module_);
  }
  
 private:
  ScopedJavaGlobalRef<jobject> j_module_;
};

/**
 * Implementation of ExternalAudioOutputCallback that calls Java methods.
 * 
 * This class bridges the C++ callback interface to Java method calls.
 * It holds a global reference to the Java ExternalAudioDeviceModule object.
 */
class AudioOutputCallbackImpl : public ExternalAudioOutputCallback {
 public:
  AudioOutputCallbackImpl(JNIEnv* env, const JavaParamRef<jobject>& j_module,
                          int /* sample_rate */, int /* channels */)
      : j_module_(env, j_module) {
    RTC_LOG(LS_INFO) << "AudioOutputCallbackImpl::ctor";
  }
  
  ~AudioOutputCallbackImpl() override {
    RTC_LOG(LS_INFO) << "AudioOutputCallbackImpl::dtor";
  }
  
  void OnPlayoutStarted() override {
    RTC_LOG(LS_INFO) << "AudioOutputCallbackImpl::OnPlayoutStarted";
    JNIEnv* env = jni_zero::AttachCurrentThread();
    Java_ExternalAudioDeviceModule_onPlayoutStartedFromNative(env, j_module_);
  }
  
  void OnPlayoutStopped() override {
    RTC_LOG(LS_INFO) << "AudioOutputCallbackImpl::OnPlayoutStopped";
    JNIEnv* env = jni_zero::AttachCurrentThread();
    Java_ExternalAudioDeviceModule_onPlayoutStoppedFromNative(env, j_module_);
  }
  
 private:
  ScopedJavaGlobalRef<jobject> j_module_;
};

/**
 * Helper structure to hold callback instances and audio input reference.
 * This ensures proper lifetime management of the callback objects.
 */
struct ExternalAudioContext {
  std::unique_ptr<AudioInputCallbackImpl> input_callback;
  std::unique_ptr<AudioOutputCallbackImpl> output_callback;
  ExternalAudioInput* audio_input = nullptr;
  ExternalAudioOutput* audio_output = nullptr;
  
  ExternalAudioContext() = default;
  ~ExternalAudioContext() = default;
};

// Global context pointer - use raw pointer to avoid global destructor warning
// The context is explicitly managed (created in Create, never deleted for simplicity)
ExternalAudioContext* g_audio_context = nullptr;

}  // namespace

/**
 * JNI entry point: Create the native AudioDeviceModule from ExternalAudioDeviceModule.java
 * 
 * This creates:
 * - AudioInputCallbackImpl: bridges C++ callbacks to Java
 * - AudioOutputCallbackImpl: bridges C++ callbacks to Java
 * - ExternalAudioInput: receives audio data from Java
 * - ExternalAudioOutput: sends audio data to Java
 * - AudioDeviceModule: glues them together using CreateAudioDeviceModuleFromInputAndOutput
 */
static jlong JNI_ExternalAudioDeviceModule_CreateAudioDeviceModule(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_external_audio_module,
    jlong j_webrtc_env_ref,
    jint sample_rate,
    jint channels) {
  
  RTC_LOG(LS_INFO) << "JNI_ExternalAudioDeviceModule_NativeCreateAudioDeviceModule: "
                   << "sample_rate=" << sample_rate << ", channels=" << channels;
  
  // Get WebRTC Environment reference
  const Environment* webrtc_env = reinterpret_cast<const Environment*>(j_webrtc_env_ref);
  RTC_DCHECK(webrtc_env != nullptr);
  
  // Create audio parameters
  // frames_per_buffer = sample_rate / 100 for 10ms of audio
  const int frames_per_buffer = sample_rate / 100;
  AudioParameters input_parameters(sample_rate, channels, frames_per_buffer);
  AudioParameters output_parameters(sample_rate, channels, frames_per_buffer);
  
  // Create context to hold callback instances (use raw pointer to avoid global destructor)
  // Note: In a real implementation, proper cleanup should be handled
  g_audio_context = new ExternalAudioContext();
  
  // Create callback implementations
  // Pass sample_rate and channels for correct buffer allocation and byte size calculation
  g_audio_context->input_callback = std::make_unique<AudioInputCallbackImpl>(
      env, j_external_audio_module, sample_rate, channels);
  g_audio_context->output_callback = std::make_unique<AudioOutputCallbackImpl>(
      env, j_external_audio_module, sample_rate, channels);
  
  RTC_LOG(LS_INFO) << "Creating ExternalAudioInput with frames_per_buffer=" << frames_per_buffer;
  
  // Create external audio input with callback
  auto audio_input = std::make_unique<ExternalAudioInput>(
      env, input_parameters, g_audio_context->input_callback.get());
  
  // Store reference for JNI callbacks
  g_audio_context->audio_input = audio_input.get();
  
  RTC_LOG(LS_INFO) << "Creating ExternalAudioOutput";
  
  // Create external audio output with callback
  auto audio_output = std::make_unique<ExternalAudioOutput>(
      env, output_parameters, g_audio_context->output_callback.get());
  
  RTC_LOG(LS_INFO) << "Creating AudioDeviceModule from input and output";
  
  // Create AudioDeviceModule using the existing factory function
  // This reuses WebRTC's internal AudioDeviceModule implementation
  g_audio_context->audio_output = audio_output.get();
  scoped_refptr<AudioDeviceModule> adm = CreateAudioDeviceModuleFromInputAndOutput(
      *webrtc_env,
      AudioDeviceModule::kAndroidJavaAudio,  // audio_layer
      channels == 2,  // is_stereo_playout_supported
      channels == 2,  // is_stereo_record_supported
      50,  // playout_delay_ms (typical Android latency)
      std::move(audio_input),
      std::move(audio_output));
  
  if (adm == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create AudioDeviceModule";
    delete g_audio_context;
    g_audio_context = nullptr;
    return 0;
  }
  
  RTC_LOG(LS_INFO) << "AudioDeviceModule created successfully";
  
  // Return as raw pointer (caller takes ownership via ref counting)
  return NativeToJavaPointer(adm.release());
}

/**
 * JNI entry point: Deliver recorded audio data from Java to native.
 * 
 * Called from ExternalAudioDeviceModule.deliverRecordedData() in Java.
 * This is a static method that receives the native pointer as parameter.
 * 
 * Note: Must NOT be declared static - jni_zero expects external linkage.
 */
void JNI_ExternalAudioDeviceModule_DeliverRecordedDataToNative(
    JNIEnv* env,
    jlong j_native_ptr,
    const JavaParamRef<jobject>& j_byte_buffer,
    jint length,
    jlong capture_timestamp_ns) {
  
  // Route to the ExternalAudioInput instance via context
  if (g_audio_context != nullptr && g_audio_context->audio_input != nullptr) {
    g_audio_context->audio_input->OnRecordedData(env, j_byte_buffer, length, capture_timestamp_ns);
  } else {
    RTC_LOG(LS_WARNING) << "NativeOnRecordedData: audio_input is null";
  }
}

jint JNI_ExternalAudioDeviceModule_PullPlayoutDataFromNative(
    JNIEnv* env,
    jlong /* j_native_ptr */,
    const JavaParamRef<jobject>& j_byte_buffer,
    jint num_samples) {
  if (g_audio_context == nullptr || g_audio_context->audio_output == nullptr) {
    return 0;
  }

  void* buffer_address = env->GetDirectBufferAddress(j_byte_buffer.obj());
  if (buffer_address == nullptr) {
    RTC_LOG(LS_ERROR)
        << "PullPlayoutDataFromNative: GetDirectBufferAddress returned null";
    return 0;
  }

  return static_cast<jint>(g_audio_context->audio_output->PullPlayoutData(
      buffer_address, static_cast<size_t>(num_samples)));
}

}  // namespace jni
}  // namespace webrtc
