/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc.audio;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.webrtc.CalledByNative;
import org.webrtc.JniCommon;
import org.webrtc.Logging;

/**
 * External AudioDeviceModule that allows APP layer to control audio capture and
 * playback.
 * 
 * Unlike JavaAudioDeviceModule which directly uses AudioRecord/AudioTrack,
 * this module delegates audio I/O to external Java callbacks, enabling:
 * - APP layer audio source switching (mic/system audio/media)
 * - APP layer audio processing
 * - APP layer playback device control (speaker/earpiece)
 * 
 * Architecture symmetry with ExternalVideoCapturer:
 * - Video: Surface -> ExternalVideoCapturer -> VideoSource
 * - Audio: ByteBuffer -> ExternalAudioDeviceModule -> AudioTrack
 */
public class ExternalAudioDeviceModule implements AudioDeviceModule {
    private static final String TAG = "ExternalAudioDeviceModule";

    /**
     * Callback interface for external audio data bridge.
     * Implemented by APP layer to provide/receive audio data.
     * 
     * Recording supports two modes:
     * 1. PUSH mode: APP calls deliverRecordedData() to push audio to WebRTC
     * 2. PULL mode: WebRTC calls onRecordData() to pull audio from APP (preferred)
     */
    public interface AudioBridge {
        /**
         * Called when recording starts. APP should start capturing audio.
         */
        void onRecordingStarted();

        /**
         * Called when recording stops. APP should stop capturing audio.
         */
        void onRecordingStopped();

        /**
         * Called when playout starts. APP should prepare for audio playback.
         */
        void onPlayoutStarted();

        /**
         * Called when playout stops. APP should stop audio playback.
         */
        void onPlayoutStopped();

        /**
         * Called to deliver decoded audio for playback.
         * 
         * @param buffer  Direct ByteBuffer containing PCM audio data
         * @param samples Number of samples in buffer
         */
        void onPlaybackData(ByteBuffer buffer, int samples);

        /**
         * PULL mode: Called by WebRTC to request recorded audio data.
         * APP should fill the buffer with captured audio samples.
         * 
         * @param buffer  Direct ByteBuffer to fill (16-bit PCM, native byte order)
         * @param samples Number of samples requested
         * @return Number of samples actually provided (0 if no data available)
         */
        default int onRecordData(ByteBuffer buffer, int samples) {
            // Default implementation returns 0 (no data)
            // Override this for PULL mode support
            return 0;
        }
    }

    // Audio format constants
    public static final int DEFAULT_SAMPLE_RATE = 48000;
    public static final int DEFAULT_CHANNELS = 1;
    public static final int DEFAULT_BYTES_PER_SAMPLE = 2; // 16-bit PCM

    private final int sampleRate;
    private final int channels;
    private volatile AudioBridge audioBridge;
    private volatile long nativeAudioDeviceModule;
    private final Object nativeLock = new Object();

    // Direct buffers for zero-copy JNI
    private ByteBuffer recordBuffer;

    /**
     * Builder for ExternalAudioDeviceModule with default values.
     */
    public static class Builder {
        private int sampleRate = DEFAULT_SAMPLE_RATE;
        private int channels = DEFAULT_CHANNELS;

        public Builder setSampleRate(int sampleRate) {
            this.sampleRate = sampleRate;
            return this;
        }

        public Builder setChannels(int channels) {
            this.channels = channels;
            return this;
        }

        public ExternalAudioDeviceModule build() {
            return new ExternalAudioDeviceModule(sampleRate, channels);
        }
    }

    /**
     * Create a new ExternalAudioDeviceModule with specified parameters.
     * 
     * @param sampleRate Audio sample rate in Hz (e.g., 48000)
     * @param channels   Number of audio channels (1=mono, 2=stereo)
     */
    public ExternalAudioDeviceModule(int sampleRate, int channels) {
        this.sampleRate = sampleRate;
        this.channels = channels;
        initBuffers();
        Logging.d(TAG, "Created ExternalAudioDeviceModule: sampleRate=" + sampleRate + ", channels=" + channels);
    }

    private void initBuffers() {
        // 10ms of audio at given sample rate
        int samplesPerFrame = sampleRate / 100;
        int bytesPerFrame = samplesPerFrame * channels * DEFAULT_BYTES_PER_SAMPLE;

        recordBuffer = ByteBuffer.allocateDirect(bytesPerFrame);
        recordBuffer.order(ByteOrder.nativeOrder());

        Logging.d(TAG, "Initialized buffers: samplesPerFrame=" + samplesPerFrame + ", bytesPerFrame=" + bytesPerFrame);
    }

    /**
     * Set the audio bridge for external audio data exchange.
     * 
     * @param bridge Implementation of AudioBridge interface
     */
    public void setAudioBridge(AudioBridge bridge) {
        this.audioBridge = bridge;
        Logging.d(TAG, "AudioBridge set: " + (bridge != null ? "non-null" : "null"));
    }

    /**
     * Get the current audio bridge.
     * 
     * @return Current AudioBridge or null
     */
    public AudioBridge getAudioBridge() {
        return audioBridge;
    }

    /**
     * Get the sample rate.
     * 
     * @return Sample rate in Hz
     */
    public int getSampleRate() {
        return sampleRate;
    }

    /**
     * Get the number of channels.
     * 
     * @return Number of channels
     */
    public int getChannels() {
        return channels;
    }

    /**
     * Get the record buffer for writing captured audio data.
     * APP layer should use this buffer to avoid memory allocation.
     * 
     * @return Direct ByteBuffer for recording
     */
    public ByteBuffer getRecordBuffer() {
        return recordBuffer;
    }

    public int pullPlayoutData(ByteBuffer buffer, int samples) {
        synchronized (nativeLock) {
            if (nativeAudioDeviceModule != 0) {
                return nativePullPlayoutDataFromNative(nativeAudioDeviceModule, buffer, samples);
            }
        }
        return 0;
    }

    /**
     * Called by APP layer to deliver captured audio data to WebRTC.
     * The buffer should be filled with 10ms of PCM audio data.
     * 
     * @param buffer             Direct ByteBuffer containing captured PCM data
     * @param samples            Number of samples (per channel)
     * @param captureTimestampNs Capture timestamp in nanoseconds
     */
    public void deliverRecordedData(ByteBuffer buffer, int samples, long captureTimestampNs) {
        synchronized (nativeLock) {
            if (nativeAudioDeviceModule != 0) {
                int length = samples * channels * DEFAULT_BYTES_PER_SAMPLE;
                nativeDeliverRecordedDataToNative(nativeAudioDeviceModule, buffer, length, captureTimestampNs);
            }
        }
    }

    /**
     * Convenience method to deliver recorded data using internal buffer.
     * APP layer should fill getRecordBuffer() before calling this.
     * 
     * @param samples            Number of samples (per channel)
     * @param captureTimestampNs Capture timestamp in nanoseconds
     */
    public void deliverRecordedData(int samples, long captureTimestampNs) {
        deliverRecordedData(recordBuffer, samples, captureTimestampNs);
    }

    // ==================== Native Callbacks ====================

    /**
     * Called from native when recording should start.
     */
    @CalledByNative
    private void onRecordingStartedFromNative() {
        Logging.d(TAG, "onRecordingStartedFromNative");
        AudioBridge bridge = audioBridge;
        if (bridge != null) {
            bridge.onRecordingStarted();
        }
    }

    /**
     * Called from native when recording should stop.
     */
    @CalledByNative
    private void onRecordingStoppedFromNative() {
        Logging.d(TAG, "onRecordingStoppedFromNative");
        AudioBridge bridge = audioBridge;
        if (bridge != null) {
            bridge.onRecordingStopped();
        }
    }

    /**
     * Called from native when playout should start.
     */
    @CalledByNative
    private void onPlayoutStartedFromNative() {
        Logging.d(TAG, "onPlayoutStartedFromNative");
        AudioBridge bridge = audioBridge;
        if (bridge != null) {
            bridge.onPlayoutStarted();
        }
    }

    /**
     * Called from native when playout should stop.
     */
    @CalledByNative
    private void onPlayoutStoppedFromNative() {
        Logging.d(TAG, "onPlayoutStoppedFromNative");
        AudioBridge bridge = audioBridge;
        if (bridge != null) {
            bridge.onPlayoutStopped();
        }
    }

    // ==================== AudioDeviceModule Interface ====================

    @Override
    public long getNative(long webrtcEnvRef) {
        synchronized (nativeLock) {
            if (nativeAudioDeviceModule == 0) {
                nativeAudioDeviceModule = nativeCreateAudioDeviceModule(
                        this, webrtcEnvRef, sampleRate, channels);
                Logging.d(TAG, "Created native audio device module: " + nativeAudioDeviceModule);
            }
            return nativeAudioDeviceModule;
        }
    }

    @Override
    public void release() {
        synchronized (nativeLock) {
            if (nativeAudioDeviceModule != 0) {
                Logging.d(TAG, "Releasing native audio device module");
                JniCommon.nativeReleaseRef(nativeAudioDeviceModule);
                nativeAudioDeviceModule = 0;
            }
        }
        audioBridge = null;
        recordBuffer = null;
    }

    @Override
    public void setSpeakerMute(boolean mute) {
        // Delegated to APP layer via AudioBridge
        Logging.d(TAG, "setSpeakerMute: " + mute + " (delegated to APP layer)");
    }

    @Override
    public void setMicrophoneMute(boolean mute) {
        // Delegated to APP layer via AudioBridge
        Logging.d(TAG, "setMicrophoneMute: " + mute + " (delegated to APP layer)");
    }

    // ==================== Native Methods ====================

    private static native long nativeCreateAudioDeviceModule(
            ExternalAudioDeviceModule module, long webrtcEnvRef, int sampleRate, int channels);

    private static native void nativeDeliverRecordedDataToNative(
            long audioDeviceModulePtr, ByteBuffer buffer, int length, long captureTimestampNs);

    private static native int nativePullPlayoutDataFromNative(
            long audioDeviceModulePtr, ByteBuffer buffer, int samples);
}
