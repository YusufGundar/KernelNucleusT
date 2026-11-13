// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

/*
----------------------------
knst_microphone.hpp
----------------------------
    Comprehensive microphone capture and voice processing.

    SCOPE
    =====
      • Microphone enumeration (filters out monitors / loopbacks)
      • Volume / mute / port / profile (delegates to knst_aud::audio)
      • PCM/WAV capture (record_raw, record_wav, recorder class)
      • Real-time stream capture (knst_aud::stream with input direction)
      • Level measurement (peak/rms/dB/clip)
      • DC offset removal
      • Voice Activity Detection (energy + ZCR + hangover)
      • Automatic Gain Control (peak follower)
      • Noise gate (attack/release envelope)
      • Pitch detection (autocorrelation)
      • Silence detection
      • Voice-triggered recorder (VAD-gated capture to file)
      • Loopback capture (monitor source — captures speaker output)
      • Microphone array / beamforming (delay-and-sum)
      • Format conversion helpers (int16 ↔ float, resample)
      • JSON bridge

    DEPENDS ON
    ==========
      knst_audio.hpp — for stream, wav, dsp, helpers, audio::* controls

    PHILOSOPHY
    ==========
      Synchronous. No hidden threads. Every call returns result<T>.
      Speech processing state machines (VAD, AGC, gate) are STEP functions:
      you feed them one block at a time, they update internal state and
      return a decision. This lets you integrate them into any loop, with
      or without threading.
*/

#pragma once

#include "knst_audio.hpp"

#if defined(KNST_USING_PLATFORM_ANDROID)
#  include <jni.h>
#endif

// Optional logging. If <knst_printer.hpp> (or any header defining
// KNST_LOG_WARN) was included before this file, the real macro is used.
// Otherwise this fallback keeps the header self-contained (no compile
// error when the mic code logs a warning on Android capture timeout).
#if !defined(KNST_LOG_WARN)
#  define KNST_LOG_WARN(...) ((void)0)
#endif

namespace knst_mic {

using knst_dev::status;
using knst_dev::result;

// ============================================================
// Extended capture format — declared first so platform
// backends (mic_aaudio, mic_wasapi) can reference it.
// ============================================================
struct capture_format {
    enum class sample_t : uint8_t { i16 = 0, i24, i32, f32 };
    uint32_t  sample_rate = 48000;
    uint32_t  channels    = 2;
    sample_t  sample      = sample_t::i16;
    bool      loopback    = false;
    int32_t   device_id   = 0;       // 0 = default; Android AudioDeviceInfo.getId()
};

inline result<knst_byte_string> record_fmt(const knst_audio_device* device,
                                            uint32_t ms,
                                            const capture_format& f) noexcept;

// ============================================================
// Native capture backends (Android AAudio / Windows waveIn)
// ============================================================

#if defined(KNST_USING_PLATFORM_ANDROID)

// Forward declaration — mic_playback namespace is defined after mic_aaudio.
namespace mic_playback {
    result<knst_byte_string> capture(const capture_format& f, uint32_t total_frames) noexcept;
    void  set_permission(int result_code, jobject result_data);
    bool  has_permission();
}

// ─── JNI provider for mic module ────────────────────────────────────
// Three usage modes:
//   1) Define KNST_MIC_USE_PRINTER_JNI=1 AND include <knst_printer.hpp>
//      BEFORE <knst_microphone.hpp> → reuses printer's JNI context.
//   2) Define KNST_MIC_HAS_EXTERNAL_ENV=1 AND provide your own
//      `knst_mic::knst_mic_external_env()` (strong symbol beats weak).
//   3) Neither — the weak default below returns nullptr, and
//      PlaybackCapture returns status::BadState at runtime.
//      This keeps the header linkable in test programs that don't
//      need system-audio capture.
#if !defined(KNST_MIC_USE_PRINTER_JNI) && \
    !defined(KNST_MIC_HAS_EXTERNAL_ENV) && \
    !defined(KNST_MIC_EXTERNAL_ENV_WEAK_DEFINED)
#  define KNST_MIC_EXTERNAL_ENV_WEAK_DEFINED 1
namespace knst_mic {
#  if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#  endif
JNIEnv* knst_mic_external_env() { return nullptr; }
} // namespace knst_mic
#endif

// JNI helpers shared by AAudio capture + PlaybackCapture bridge.
inline JNIEnv* knst_mic_jni_env() {
    #if defined(KNST_MIC_USE_PRINTER_JNI)
    return knst_prn::detail::jni().env();
    #else
    extern JNIEnv* knst_mic_external_env();
    return knst_mic_external_env();
    #endif
}
inline jclass knst_mic_find_class(const char* slash_name) {
    JNIEnv* e = knst_mic_jni_env();
    if (!e) return nullptr;
    jclass c = e->FindClass(slash_name);
    if (e->ExceptionCheck()) { e->ExceptionClear(); return nullptr; }
    return c;
}

namespace mic_aaudio {
    struct api {
        void* h = nullptr;
        int32_t (*createBuilder)(void**) = nullptr;
        void    (*builderDelete)(void*) = nullptr;
        void    (*setDirection)(void*, int32_t) = nullptr;
        void    (*setSampleRate)(void*, int32_t) = nullptr;
        void    (*setChannelCount)(void*, int32_t) = nullptr;
        void    (*setFormat)(void*, int32_t) = nullptr;
        void    (*setPerfMode)(void*, int32_t) = nullptr;
        void    (*setDeviceId)(void*, int32_t) = nullptr;
        int32_t (*openStream)(void*, void**) = nullptr;
        int32_t (*streamRead)(void*, void*, int32_t, int64_t) = nullptr;
        int32_t (*requestStart)(void*) = nullptr;
        int32_t (*requestStop)(void*) = nullptr;
        int32_t (*streamClose)(void*) = nullptr;
        bool ok = false;
    };
    inline api& get() noexcept {
        static api a = []() {
            api r;
            r.h = ::dlopen("libaaudio.so", RTLD_LAZY);
            if (!r.h) return r;
            r.createBuilder  = (decltype(r.createBuilder))  ::dlsym(r.h, "AAudio_createStreamBuilder");
            r.builderDelete  = (decltype(r.builderDelete))  ::dlsym(r.h, "AAudioStreamBuilder_delete");
            r.setDirection   = (decltype(r.setDirection))   ::dlsym(r.h, "AAudioStreamBuilder_setDirection");
            r.setSampleRate  = (decltype(r.setSampleRate))  ::dlsym(r.h, "AAudioStreamBuilder_setSampleRate");
            r.setChannelCount= (decltype(r.setChannelCount))::dlsym(r.h, "AAudioStreamBuilder_setChannelCount");
            r.setFormat      = (decltype(r.setFormat))      ::dlsym(r.h, "AAudioStreamBuilder_setFormat");
                        r.setPerfMode    = (decltype(r.setPerfMode))    ::dlsym(r.h, "AAudioStreamBuilder_setPerformanceMode");
            r.setDeviceId    = (decltype(r.setDeviceId))    ::dlsym(r.h, "AAudioStreamBuilder_setDeviceId");
            r.openStream     = (decltype(r.openStream))     ::dlsym(r.h, "AAudioStreamBuilder_openStream");
            r.streamRead     = (decltype(r.streamRead))     ::dlsym(r.h, "AAudioStream_read");
            r.requestStart   = (decltype(r.requestStart))   ::dlsym(r.h, "AAudioStream_requestStart");
            r.requestStop    = (decltype(r.requestStop))    ::dlsym(r.h, "AAudioStream_requestStop");
            r.streamClose    = (decltype(r.streamClose))    ::dlsym(r.h, "AAudioStream_close");
            r.ok = r.createBuilder && r.builderDelete && r.setDirection && r.setSampleRate &&
                   r.setChannelCount && r.setFormat && r.openStream && r.streamRead &&
                   r.requestStart && r.requestStop && r.streamClose;
            return r;
        }();
        return a;
    }

    inline result<knst_byte_string> capture_s16le(uint32_t rate, uint32_t channels,
                                                    uint32_t total_frames,
                                                    int32_t device_id = 0) noexcept {
        using R = result<knst_byte_string>;
        auto& aa = get();
        if (!aa.ok) return R::fail(status::NotSupported);
        if (total_frames == 0 || channels == 0 || channels > 8) return R::fail(status::InvalidArgument);

        void* builder = nullptr;
        if (aa.createBuilder(&builder) != 0 || !builder) return R::fail(status::Unknown);
        aa.setDirection(builder, 1);              // INPUT
        aa.setSampleRate(builder, (int32_t)rate);
        aa.setChannelCount(builder, (int32_t)channels);
        aa.setFormat(builder, 1);                 // I16
        aa.setPerfMode(builder, 12);              // LOW_LATENCY
        if (device_id > 0) {
            aa.setDeviceId(builder, device_id);  
        }
        void* sh = nullptr;
        int32_t rc = aa.openStream(builder, &sh);
        aa.builderDelete(builder);
        if (rc != 0 || !sh) return R::fail(status::Unknown, rc);
        if (aa.requestStart(sh) != 0) { aa.streamClose(sh); return R::fail(status::Unknown); }

        const uint32_t want_bytes = total_frames * channels * 2;
        knst_byte_string out;
        out.resize(want_bytes);
        if (out.length() < want_bytes) return R::fail(status::OutOfMemory);
        int16_t* dst = reinterpret_cast<int16_t*>(&out[0]);
        uint32_t got = 0;
        const uint32_t chunk = 1024;
        // Timeout = expected duration + 5s margin. Previously hard-coded at
        // 10s, which silently truncated any recording longer than 10 seconds
        // (returned SUCCESS with zero-padded tail). Now scales with the
        // requested length so 30s/60s/5min captures complete properly.
        const uint32_t rate_guard = rate ? rate : 1;
        const uint32_t timeout_ms =
            (uint32_t)((uint64_t)total_frames * 1000 / rate_guard) + 5000;
        auto t_cap_start = std::chrono::steady_clock::now();
        while (got < total_frames) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t_cap_start).count();
            if ((uint32_t)elapsed_ms > timeout_ms) {
                break;
            }
            uint32_t want = (total_frames - got < chunk) ? (total_frames - got) : chunk;
            int32_t n = aa.streamRead(sh, dst + got * channels, (int32_t)want, 500000000LL);
            if (n <= 0) break;
            got += (uint32_t)n;
        }
        aa.requestStop(sh);
        aa.streamClose(sh);
        if (got < total_frames)
            ::memset(dst + got * channels, 0, (total_frames - got) * channels * 2);
        return R::success(std::move(out));
    }

    // Float32 variant — AAudio native supports AAUDIO_FORMAT_PCM_FLOAT = 2.
    inline result<knst_byte_string> capture_f32(uint32_t rate, uint32_t channels,
                                                  uint32_t total_frames) noexcept {
        using R = result<knst_byte_string>;
        auto& aa = get();
        if (!aa.ok) return R::fail(status::NotSupported);
        if (total_frames == 0 || channels == 0 || channels > 8) return R::fail(status::InvalidArgument);

        void* builder = nullptr;
        if (aa.createBuilder(&builder) != 0 || !builder) return R::fail(status::Unknown);
        aa.setDirection(builder, 1);
        aa.setSampleRate(builder, (int32_t)rate);
        aa.setChannelCount(builder, (int32_t)channels);
        aa.setFormat(builder, 2);             // FLOAT
        aa.setPerfMode(builder, 12);
        void* sh = nullptr;
        int32_t rc = aa.openStream(builder, &sh);
        aa.builderDelete(builder);
        if (rc != 0 || !sh) return R::fail(status::Unknown, rc);
        if (aa.requestStart(sh) != 0) { aa.streamClose(sh); return R::fail(status::Unknown); }

        const uint32_t want_bytes = total_frames * channels * 4;
        knst_byte_string out;
        out.resize(want_bytes);
        if (out.length() < want_bytes) { aa.streamClose(sh); return R::fail(status::OutOfMemory); }
        float* dst = reinterpret_cast<float*>(&out[0]);
        uint32_t got = 0;
        const uint32_t chunk = 1024;
        const uint32_t rate_guard = rate ? rate : 1;
        const uint32_t timeout_ms =
            (uint32_t)((uint64_t)total_frames * 1000 / rate_guard) + 5000;
        auto t0 = std::chrono::steady_clock::now();
        while (got < total_frames) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if ((uint32_t)elapsed > timeout_ms) break;
            uint32_t want = (total_frames - got < chunk) ? (total_frames - got) : chunk;
            int32_t n = aa.streamRead(sh, dst + got * channels, (int32_t)want, 500000000LL);
            if (n <= 0) break;
            got += (uint32_t)n;
        }
        aa.requestStop(sh);
        aa.streamClose(sh);
        if (got < total_frames)
            ::memset(dst + got * channels, 0, (total_frames - got) * channels * 4);
        return R::success(std::move(out));
    }

    // Extended dispatch: multi-format + loopback.
    inline result<knst_byte_string> capture_ext(
            const capture_format& f, uint32_t total_frames,
            const knst_audio_device* device) noexcept {
        using R = result<knst_byte_string>;
        // Loopback → route to Java PlaybackCapture helper.
        if (f.loopback) {
            return mic_playback::capture(f, total_frames);
        }
        // Non-loopback: AAudio.
        if (device && !device->name.empty()) {
            // AAudio only has default input; named device selection not exposed.
        }
        switch (f.sample) {
            case capture_format::sample_t::i16:
                return capture_s16le(f.sample_rate, f.channels, total_frames, f.device_id);
            case capture_format::sample_t::f32:
                return capture_f32(f.sample_rate, f.channels, total_frames);
            case capture_format::sample_t::i24:
            case capture_format::sample_t::i32:
            default:
                return R::fail(status::NotSupported);
        }
    }
} // namespace mic_aaudio

// ─────────────────────────────────────────────────────────────────────────────
//  Android AudioPlaybackCapture bridge (requires app integration)
//
//  The app must:
//    1) mpm = context.getSystemService(MEDIA_PROJECTION_SERVICE)
//    2) intent = mpm.createScreenCaptureIntent()
//    3) startActivityForResult(intent, REQ_CAPTURE)
//    4) In onActivityResult: set_playback_permission(resultCode, data)
//
//  Then record_fmt(…, loopback=true) works.
// ─────────────────────────────────────────────────────────────────────────────
namespace mic_playback {

// Call once after the app receives the MediaProjection permission result.
inline void set_permission(int result_code, jobject result_data) {
    JNIEnv* e = knst_mic_jni_env();
    if (!e) return;
    jclass helper = knst_mic_find_class("com/knst/print/KnstPlaybackCapture");
    if (!helper) return;
    jmethodID m = e->GetStaticMethodID(helper, "setProjection",
        "(ILandroid/content/Intent;)V");
    if (m) e->CallStaticVoidMethod(helper, m, (jint)result_code, result_data);
    if (e->ExceptionCheck()) e->ExceptionClear();
    e->DeleteLocalRef(helper);
}

inline bool has_permission() {
    JNIEnv* e = knst_mic_jni_env();
    if (!e) return false;
    jclass helper = knst_mic_find_class("com/knst/print/KnstPlaybackCapture");
    if (!helper) return false;
    jmethodID m = e->GetStaticMethodID(helper, "hasProjection", "()Z");
    bool ok = m && e->CallStaticBooleanMethod(helper, m);
    if (e->ExceptionCheck()) { e->ExceptionClear(); ok = false; }
    e->DeleteLocalRef(helper);
    return ok;
}

inline result<knst_byte_string> capture(const capture_format& f,
                                          uint32_t total_frames) noexcept {
    using R = result<knst_byte_string>;
    JNIEnv* e = knst_mic_jni_env();
    if (!e) return R::fail(status::BadState);
    jclass helper = knst_mic_find_class("com/knst/print/KnstPlaybackCapture");
    if (!helper) return R::fail(status::NotSupported);
    jmethodID m = e->GetStaticMethodID(helper, "capture",
        "(Ljava/lang/String;IIII)[B");
    if (!m) { e->DeleteLocalRef(helper); return R::fail(status::NotSupported); }

    // Temp path for the Java side to write raw PCM into
    knst_c16string tmp = knst_file::join_path(
        knst_file::get_temp_dir(), u"knst_playcap.pcm");
    knst_byte_string tmp_u8 = knst_dev::to_utf8(tmp);
    jstring jpath = e->NewStringUTF(reinterpret_cast<const char*>(tmp_u8.data()));
    int sfmt = (int)f.sample;
    uint32_t bytes = total_frames * f.channels * (
        f.sample == capture_format::sample_t::i16 ? 2 : 4);
    jobject arr = e->CallStaticObjectMethod(helper, m, jpath,
        (jint)f.sample_rate, (jint)f.channels, (jint)sfmt, (jint)total_frames);

    e->DeleteLocalRef(jpath);
    e->DeleteLocalRef(helper);

    if (e->ExceptionCheck()) { e->ExceptionClear(); return R::fail(status::IoError); }
    if (!arr) return R::fail(status::IoError);

    knst_file_error fe = knst_file_error::None;
    knst_byte_string out = knst_file::read_file_data<>(tmp, &fe);
    knst_file::remove_file(tmp);
    if (fe != knst_file_error::None) return R::fail(status::IoError);
    if (out.length() < bytes) {
        uint32_t old = out.length();
        out.resize(bytes);
        ::memset(&out[old], 0, bytes - old);
    } else if (out.length() > bytes) {
        out.resize(bytes);
    }
    return R::success(std::move(out));
}

} // namespace mic_playback
#endif // KNST_USING_PLATFORM_ANDROID

#if KNST_USING_PLATFORM_WINDOWS
// WASAPI headers (mmdeviceapi + audioclient) for loopback + multi-format.
#  include <mmdeviceapi.h>
#  include <audioclient.h>
// ole32.lib is already linked by KernelNucleusT's CMake for Windows.

namespace mic_wavein {
    inline result<knst_byte_string> capture_s16le(uint32_t rate, uint32_t channels,
                                                    uint32_t total_frames) noexcept {
        using R = result<knst_byte_string>;
        if (total_frames == 0 || channels == 0 || channels > 8) return R::fail(status::InvalidArgument);
        WAVEFORMATEX wf = {};
        wf.wFormatTag      = WAVE_FORMAT_PCM;
        wf.nChannels       = (WORD)channels;
        wf.nSamplesPerSec  = rate;
        wf.wBitsPerSample  = 16;
        wf.nBlockAlign     = (WORD)(channels * 2);
        wf.nAvgBytesPerSec = rate * channels * 2;
        HWAVEIN hwi = nullptr;
        MMRESULT rc = ::waveInOpen(&hwi, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
        if (rc != MMSYSERR_NOERROR) return R::fail(status::NotFound, (int)rc);
        const uint32_t want_bytes = total_frames * channels * 2;
        knst_byte_string out; out.resize(want_bytes);
        WAVEHDR hdr = {};
        hdr.lpData = (LPSTR)out.data();
        hdr.dwBufferLength = want_bytes;
        rc = ::waveInPrepareHeader(hwi, &hdr, sizeof(hdr));
        if (rc == MMSYSERR_NOERROR) rc = ::waveInAddBuffer(hwi, &hdr, sizeof(hdr));
        if (rc == MMSYSERR_NOERROR) rc = ::waveInStart(hwi);
        if (rc != MMSYSERR_NOERROR) {
            ::waveInReset(hwi); ::waveInUnprepareHeader(hwi, &hdr, sizeof(hdr));
            ::waveInClose(hwi); return R::fail(status::Unknown, (int)rc);
        }
        const ULONGLONG deadline = ::GetTickCount64() + 60000;
        while (!(hdr.dwFlags & WHDR_DONE)) {
            if (::GetTickCount64() > deadline) break;
            ::Sleep(5);
        }
        ::waveInReset(hwi);
        ::waveInUnprepareHeader(hwi, &hdr, sizeof(hdr));
        ::waveInClose(hwi);

        // On timeout or underrun, the driver only fills hdr.dwBytesRecorded
        // bytes; the rest of `out` is uninitialized stack/heap memory.
        // Zero-fill the tail so callers never see garbage samples.
        if (hdr.dwBytesRecorded < want_bytes) {
                        ::memset(const_cast<uint8_t*>(
                         reinterpret_cast<const uint8_t*>(out.data()))
                     + hdr.dwBytesRecorded,
                     0, want_bytes - hdr.dwBytesRecorded);
        }
        return R::success(std::move(out));
    }
}
#endif

#if KNST_USING_PLATFORM_WINDOWS
// WASAPI capture — required for:
//   • loopback (system playback)
//   • i24 / i32 / f32 sample formats
//   • any modern capture (waveIn is legacy / MME)
namespace mic_wasapi {

namespace _w {
    struct com_scope {
        HRESULT hr = E_FAIL;
        bool    needs_uninit = false;
        com_scope() {
            hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            needs_uninit = (hr == S_OK);
        }
        ~com_scope() { if (needs_uninit) ::CoUninitialize(); }
        com_scope(const com_scope&) = delete;
        com_scope& operator=(const com_scope&) = delete;
    };
    template <class T> struct rscope {
        T* p = nullptr;
        ~rscope() { if (p) p->Release(); }
        T** operator&() { return &p; }
    };

    inline uint32_t bytes_per_sample(capture_format::sample_t s) noexcept {
        switch (s) {
            case capture_format::sample_t::i24: return 3;
            case capture_format::sample_t::i32: return 4;
            case capture_format::sample_t::f32: return 4;
            case capture_format::sample_t::i16:
            default:                            return 2;
        }
    }

    // Convert float32 samples (WASAPI shared-mode native) → requested format.
    inline void convert(const float* src, uint32_t n, capture_format::sample_t want,
                        knst_byte_string& out) {
        out.clear();
        const uint32_t bps = bytes_per_sample(want);
        out.resize(n * bps);
        uint8_t* p = reinterpret_cast<uint8_t*>(&out[0]);
        switch (want) {
            case capture_format::sample_t::f32: {
                for (uint32_t i = 0; i < n; ++i) {
                    float v = src[i];
                    ::memcpy(p + i * 4, &v, 4);
                }
                break;
            }
            case capture_format::sample_t::i32: {
                for (uint32_t i = 0; i < n; ++i) {
                    float v = src[i];
                    if (v >  1.0f) v =  1.0f;
                    if (v < -1.0f) v = -1.0f;
                    // Double literal (not .0f) — 2147483647 cannot be exactly
                    // represented in float; .0f rounds to 2^31, which overflows
                    // int32_t on v=1.0 → INT_MIN.
                    int32_t s = (int32_t)std::lround(v * 2147483647.0);
                    p[i * 4]     = (uint8_t)(s & 0xFF);
                    p[i * 4 + 1] = (uint8_t)((s >> 8) & 0xFF);
                    p[i * 4 + 2] = (uint8_t)((s >> 16) & 0xFF);
                    p[i * 4 + 3] = (uint8_t)((s >> 24) & 0xFF);
                }
                break;
            }
            case capture_format::sample_t::i24: {
                for (uint32_t i = 0; i < n; ++i) {
                    float v = src[i];
                    if (v >  1.0f) v =  1.0f;
                    if (v < -1.0f) v = -1.0f;
                    int32_t s = (int32_t)std::lround(v * 8388607.0f);
                    p[i * 3]     = (uint8_t)(s & 0xFF);
                    p[i * 3 + 1] = (uint8_t)((s >> 8) & 0xFF);
                    p[i * 3 + 2] = (uint8_t)((s >> 16) & 0xFF);
                }
                break;
            }
            case capture_format::sample_t::i16:
            default: {
                for (uint32_t i = 0; i < n; ++i) {
                    float v = src[i];
                    if (v >  1.0f) v =  1.0f;
                    if (v < -1.0f) v = -1.0f;
                    int16_t s = (int16_t)std::lround(v * 32767.0f);
                    p[i * 2]     = (uint8_t)(s & 0xFF);
                    p[i * 2 + 1] = (uint8_t)((s >> 8) & 0xFF);
                }
                break;
            }
        }
    }

        // Resolve a device by name. `knst_audio_device::name` on Windows is
    // the WASAPI endpoint id — the "{0.0.0.00000000}.{...}" GUID string
    // returned by IMMDevice::GetId() — so it can be passed straight to
    // IMMDeviceEnumerator::GetDevice(). nullptr/empty name → default.
    //
    // Earlier versions returned E_NOTIMPL here, which meant that any
    // explicit device selection in record_raw / record_wav silently
    // fell back to waveIn and captured the default mic. That hid user
    // intent ("record from my USB mic") — now we honor the request.
    inline HRESULT resolve_device(IMMDeviceEnumerator* en, EDataFlow flow,
                                   const knst_audio_device* dev, IMMDevice** out) {
        if (dev && !dev->name.empty()) {
#if KNST_USING_PLATFORM_WINDOWS
            // LPCWSTR and knst_c16string::data() are both 16-bit on Windows.
            return en->GetDevice(
                reinterpret_cast<LPCWSTR>(dev->name.data()), out);
#else
            return E_NOTIMPL;   // unreachable: this branch is Windows-only
#endif
        }
        return en->GetDefaultAudioEndpoint(flow, eConsole, out);
    }
} // namespace _w

inline result<knst_byte_string> capture(
        const capture_format& f, uint32_t total_frames,
        const knst_audio_device* device) noexcept {
    using R = result<knst_byte_string>;
    if (total_frames == 0) return R::fail(status::InvalidArgument);

    _w::com_scope cs;
    if (!cs.needs_uninit && cs.hr == RPC_E_CHANGED_MODE) {
        // COM already initialized in different mode — OK, we still have access
    } else if (FAILED(cs.hr) && cs.hr != RPC_E_CHANGED_MODE) {
        return R::fail(status::NotSupported, (int)cs.hr);
    }

    _w::rscope<IMMDeviceEnumerator> en;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof(IMMDeviceEnumerator),
                                     reinterpret_cast<void**>(&en.p));
    if (FAILED(hr) || !en.p) return R::fail(status::Unknown, (int)hr);

    _w::rscope<IMMDevice> endpt;
    EDataFlow flow = f.loopback ? eRender : eCapture;
    hr = _w::resolve_device(en.p, flow, device, &endpt.p);
    if (hr == E_NOTIMPL) return R::fail(status::NotSupported);
    if (FAILED(hr) || !endpt.p) return R::fail(status::NotFound, (int)hr);

    _w::rscope<IAudioClient> ac;
    hr = endpt.p->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&ac.p));
    if (FAILED(hr) || !ac.p) return R::fail(status::Unknown, (int)hr);

    WAVEFORMATEX* mix = nullptr;
    hr = ac.p->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) return R::fail(status::Unknown, (int)hr);

    // Shared-mode mix format on Vista+ is almost always WAVE_FORMAT_IEEE_FLOAT,
    // but some exotic drivers use 16-bit PCM. Our _w::convert reads the
    // buffer as float32, so verifying the format here prevents garbage output.
    bool is_float_mix = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                        (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                         mix->cbSize >= 22 &&
                         IsEqualGUID(
                            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat,
                            knst_aud::detail_wasapi::subtype_float));
    if (!is_float_mix) {
        ::CoTaskMemFree(mix);
        return R::fail(status::NotSupported);
    }

    // The shared-mode mix format dictates the ACTUAL channel count of
    // `data`. If the caller requested a different count we must
    // downmix / upmix BEFORE sample-format conversion — otherwise the
    // interleaved float buffer is read with the wrong stride.
    const UINT32 mix_channels = mix->nChannels;

    REFERENCE_TIME dur = (REFERENCE_TIME)total_frames * 10000000LL / (LONGLONG)f.sample_rate;
    if (dur < 1000000) dur = 1000000;   // at least 100ms buffer

    DWORD flags = f.loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = ac.p->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, dur, 0, mix, nullptr);
    ::CoTaskMemFree(mix);
    if (FAILED(hr)) return R::fail(status::Unknown, (int)hr);

    _w::rscope<IAudioCaptureClient> cc;
    hr = ac.p->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&cc.p));
    if (FAILED(hr) || !cc.p) return R::fail(status::Unknown, (int)hr);

    HANDLE ev = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (ev) ac.p->SetEventHandle(ev);

    hr = ac.p->Start();
    if (FAILED(hr)) {
        if (ev) ::CloseHandle(ev);
        return R::fail(status::Unknown, (int)hr);
    }

    const uint32_t bps = _w::bytes_per_sample(f.sample);
    const uint64_t want_bytes_u64 = (uint64_t)total_frames * f.channels * bps;
    if (want_bytes_u64 > 512u * 1024u * 1024u) {
        ac.p->Stop();
        if (ev) ::CloseHandle(ev);
        return R::fail(status::OutOfMemory);
    }
    knst_byte_string out;
    uint32_t got_frames = 0;
    auto t0 = std::chrono::steady_clock::now();
    const uint32_t timeout_ms = total_frames * 1000 / f.sample_rate + 5000;

    while (got_frames < total_frames) {
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if ((uint32_t)el > timeout_ms) break;

        UINT32 pkt = 0;
        hr = cc.p->GetNextPacketSize(&pkt);
        if (FAILED(hr)) break;
        if (pkt == 0) {
            if (ev) ::WaitForSingleObject(ev, 20);
            else ::Sleep(10);
            continue;
        }

        BYTE*  data  = nullptr;
        UINT32 frames = 0;
        DWORD  bflags = 0;
        hr = cc.p->GetBuffer(&data, &frames, &bflags, nullptr, nullptr);
        if (FAILED(hr)) break;

        uint32_t take = (total_frames - got_frames < frames)
                        ? (total_frames - got_frames) : frames;

        if (bflags & AUDCLNT_BUFFERFLAGS_SILENT) {
            uint32_t old = out.length();
            out.resize(old + take * f.channels * bps);
            ::memset(&out[old], 0, take * f.channels * bps);
        } else {
            const UINT32 src_ch    = mix_channels;
            const UINT32 want_ch   = f.channels;
            const UINT32 frames_n  = take * want_ch;

            knst_vector<float> mixed;
            mixed.resize(frames_n);
            const float* src_f = reinterpret_cast<const float*>(data);

            if (want_ch == src_ch) {
                for (UINT32 fr = 0; fr < take; ++fr)
                    for (UINT32 ch = 0; ch < want_ch; ++ch)
                        mixed[fr * want_ch + ch] = src_f[fr * src_ch + ch];
            } else if (want_ch == 1) {
                // Downmix any N -> 1 (sum then divide).
                for (UINT32 fr = 0; fr < take; ++fr) {
                    float s = 0.0f;
                    for (UINT32 ch = 0; ch < src_ch; ++ch) s += src_f[fr * src_ch + ch];
                    mixed[fr] = s / (float)src_ch;
                }
            } else if (want_ch < src_ch) {
                // Downmix N -> M (take first M).
                for (UINT32 fr = 0; fr < take; ++fr)
                    for (UINT32 ch = 0; ch < want_ch; ++ch)
                        mixed[fr * want_ch + ch] = src_f[fr * src_ch + ch];
            } else {
                // Upmix M -> N (duplicate channels cyclically).
                for (UINT32 fr = 0; fr < take; ++fr)
                    for (UINT32 ch = 0; ch < want_ch; ++ch)
                        mixed[fr * want_ch + ch] = src_f[fr * src_ch + (ch % src_ch)];
            }

            knst_byte_string conv;
            _w::convert(mixed.data(), frames_n, f.sample, conv);
            uint32_t old = out.length();
            out.resize(old + conv.length());
            ::memcpy(&out[old], conv.data(), conv.length());
        }
        cc.p->ReleaseBuffer(frames);
        got_frames += take;
    }

    ac.p->Stop();
    if (ev) ::CloseHandle(ev);

    if (got_frames < total_frames) {
        uint32_t pad = total_frames - got_frames;
        uint32_t old = out.length();
        out.resize(old + pad * f.channels * bps);
        ::memset(&out[old], 0, pad * f.channels * bps);
    }
    return R::success(std::move(out));
}

} // namespace mic_wasapi
#endif // KNST_USING_PLATFORM_WINDOWS

// ============================================================
// Microphone enumeration
// ============================================================

struct mic_ref {
    knst_c16string name;
    knst_c16string description;
    uint32_t       index = 0;
    bool           is_default = false;
    bool           is_monitor = false;    // loopback, not real mic
};

// True if the source name is a monitor (loopback), not a real microphone.
inline bool is_monitor(const knst_c16string& name) noexcept {
    return name.contains(u".monitor") || name.contains(u"Monitor");
}

// Enumerate real microphones only (monitors filtered out).
inline result<knst_vector<mic_ref>> enumerate_microphones(bool include_monitors = false) noexcept {
    using R = result<knst_vector<mic_ref>>;
    auto all = knst_aud::audio::list_sources();
    if (!all) return R::fail(all.st, all.native);

    R r;
    for (auto& d : all.value) {
        if (!d.is_input) continue;
        bool mon = is_monitor(d.name);
        if (mon && !include_monitors) continue;
        mic_ref m;
        m.name        = d.name;
        m.description = d.description;
        m.index       = d.index;
        m.is_default  = d.is_default;
        m.is_monitor  = mon;
        r.value.push_back(std::move(m));
    }
    return r;
}

// Get the default microphone (returns NotFound if only monitors exist).
inline result<knst_audio_device> default_microphone() noexcept {
    using R = result<knst_audio_device>;
    auto srcs = knst_aud::audio::list_sources();
    if (!srcs) return R::fail(srcs.st, srcs.native);
    for (auto& s : srcs.value) {
        if (s.is_default && !is_monitor(s.name)) return R::success(std::move(s));
    }
    // Fallback: first real (non-monitor) source
    for (auto& s : srcs.value) {
        if (!is_monitor(s.name)) return R::success(std::move(s));
    }
    return R::fail(status::NotFound);
}

// Get a monitor source for loopback (captures system playback).
inline result<knst_audio_device> monitor_source() noexcept {
    using R = result<knst_audio_device>;
    auto srcs = knst_aud::audio::list_sources();
    if (!srcs) return R::fail(srcs.st, srcs.native);
    for (auto& s : srcs.value)
        if (is_monitor(s.name)) return R::success(std::move(s));
    return R::fail(status::NotFound);
}

// ============================================================
// High-level recorder (moved here from knst_audio)
// ============================================================

class recorder {
public:
    enum class mode_t : uint8_t { none, file, memory };

    recorder() noexcept = default;
    ~recorder() { abort(); }

    recorder(recorder&& o) noexcept
        : mode_(o.mode_), path_(std::move(o.path_)), fmt_(o.fmt_),
          samples_(std::move(o.samples_)), total_frames_(o.total_frames_),
          hdr_len_(o.hdr_len_) {
        o.mode_ = mode_t::none;
        o.total_frames_ = 0;
        o.hdr_len_ = 44;
    }

    recorder& operator=(recorder&& o) noexcept {
        if (this != &o) {
            abort();
            mode_ = o.mode_; path_ = std::move(o.path_); fmt_ = o.fmt_;
            samples_ = std::move(o.samples_); total_frames_ = o.total_frames_;
            hdr_len_ = o.hdr_len_;
            o.mode_ = mode_t::none;
            o.total_frames_ = 0;
            o.hdr_len_ = 44;
        }
        return *this;
    }

    recorder(const recorder&) = delete;
    recorder& operator=(const recorder&) = delete;

    static result<recorder> open_file(const knst_c16string& path,
                                       const knst_aud::wav::format& f) noexcept {
        using R = result<recorder>;
        if (path.empty() || f.channels == 0 || f.rate == 0 || f.channels > 8)
            return R::fail(status::InvalidArgument);
        if (!knst_file::ensure_parent_dir(path))
            return R::fail(status::IoError);

        // Use the SAME builder as wav::encode_memory (which calls
        // codec::make_wav_header). Header length varies by format:
        //   PCM i16, ≤2ch        → 44 bytes
        //   float / alaw / ulaw  → +12 (fact chunk) = 56 bytes
        //   PCM 3+ch (extensible)→ +24 (cbSize + mask + GUID) = 68 bytes
        //   float 3+ch           → +24 +12 = 80 bytes
        // If we wrote a fixed 44-byte header here, capture_interleaved()
        // would strip the wrong offset and the appended PCM would begin
        // mid-header (garbage in the data chunk). Using make_wav_header
        // and storing hdr_len_ keeps open/capture/finish in sync.
        knst_aud::codec::wav_hdr h;
        if (!knst_aud::codec::make_wav_header(h, f.rate, f.channels, f.sample, 0, 0))
            return R::fail(status::InvalidArgument);
        if (!knst_file::write_file_data(path, h.b, h.len))
            return R::fail(status::IoError);

        recorder r;
        r.mode_    = mode_t::file;
        r.path_    = path;
        r.fmt_     = f;
        r.hdr_len_ = h.len;
        return R::success(std::move(r));
    }

    static result<recorder> open_memory(const knst_aud::wav::format& f) noexcept {
        using R = result<recorder>;
        if (f.channels == 0 || f.rate == 0 || f.channels > 8)
            return R::fail(status::InvalidArgument);
        recorder r;
        r.mode_ = mode_t::memory;
        r.fmt_  = f;
        return R::success(std::move(r));
    }

    result<uint32_t> capture_interleaved(const float* buffer, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (!buffer || frames == 0) return R::fail(status::InvalidArgument);
        if (mode_ == mode_t::none) return R::fail(status::BadState);

        if (mode_ == mode_t::memory) {
            uint32_t old_n = samples_.size();
            uint32_t new_n = old_n + frames * fmt_.channels;
            if (new_n < old_n) return R::fail(status::OutOfMemory);   // uint32 overflow
            samples_.resize(new_n);
            if (samples_.size() < new_n) return R::fail(status::OutOfMemory);
            ::memcpy(samples_.data() + old_n, buffer,
                     frames * fmt_.channels * sizeof(float));
        } else if (mode_ == mode_t::file) {
            auto bytes = knst_aud::wav::encode_memory(buffer, frames, fmt_);
            if (!bytes) return R::fail(bytes.st, bytes.native);
            if (bytes.value.length() <= hdr_len_) return R::success(0u);
            uint32_t payload = bytes.value.length() - hdr_len_;
            if (!knst_file::append_file_data(path_, bytes.value.data() + hdr_len_, payload))
                return R::fail(status::IoError);
        }
        total_frames_ += frames;
        return R::success(frames);
    }

    result<uint32_t> pump_from(knst_aud::stream& source, uint32_t frames) noexcept {
        using R = result<uint32_t>;
        if (fmt_.channels != source.channels())
            return R::fail(status::InvalidArgument);
        knst_vector<float> tmp;
        tmp.resize(frames * fmt_.channels);
        auto r = source.read_interleaved(tmp.data(), frames);
        if (!r) return r;
        if (r.value == 0) return r;
        return capture_interleaved(tmp.data(), r.value);
    }

        result<void> finish() noexcept {
        using R = result<void>;
        if (mode_ == mode_t::file) {
            // Rebuild header with the SAME builder used at open time so
            // data_bytes / fact-frames / total-riff-size are all consistent
            // with the appended payload.
            const uint32_t bps = knst_aud::sample_bytes(fmt_.sample);
            const uint64_t data_bytes = (uint64_t)total_frames_ * fmt_.channels * bps;

            knst_aud::codec::wav_hdr h;
            if (!knst_aud::codec::make_wav_header(h, fmt_.rate, fmt_.channels,
                                                  fmt_.sample, data_bytes, total_frames_))
                return R::fail(status::IoError);
        #if KNST_USING_PLATFORM_WINDOWS
            std::FILE* f = ::_wfopen(reinterpret_cast<const wchar_t*>(path_.data()), L"r+b");
        #else
            knst_byte_string pu = knst_dev::to_utf8(path_);
            std::FILE* f = std::fopen(knst_dev::text(pu), "r+b");
        #endif
            if (!f) return R::fail(status::IoError);
            std::fseek(f, 0, SEEK_SET);
            std::fwrite(h.b, 1, h.len, f);
            std::fclose(f);
        }
        mode_ = mode_t::none;
        return R::success();
    }

    void abort() noexcept {
        mode_ = mode_t::none;
        total_frames_ = 0;
        samples_.clear();
        hdr_len_ = 44;
    }

    uint32_t total_frames() const noexcept { return total_frames_; }
    double   recorded_seconds() const noexcept {
        return fmt_.rate ? (double)total_frames_ / fmt_.rate : 0.0;
    }
    const knst_aud::wav::format& fmt() const noexcept { return fmt_; }
    const knst_vector<float>& samples() const noexcept { return samples_; }
    mode_t mode() const noexcept { return mode_; }
    bool is_open() const noexcept { return mode_ != mode_t::none; }

private:
    mode_t             mode_          = mode_t::none;
    knst_c16string     path_;
    knst_aud::wav::format fmt_;
    knst_vector<float> samples_;
    uint32_t           total_frames_  = 0;
    uint32_t           hdr_len_       = 44;   // header size written by make_wav_header
};

// ============================================================
// Simple capture helpers
// ============================================================



// Backwards-compatible wrapper.
inline result<knst_byte_string> record_raw(const knst_audio_device* device,
                                            uint32_t ms,
                                            uint32_t rate = 16000,
                                            uint32_t channels = 1) noexcept {
    capture_format f{};
    f.sample_rate = rate;
    f.channels = channels;
    f.sample = capture_format::sample_t::i16;
    f.loopback = false;
    return record_fmt(device, ms, f);
}

// ─── Legacy record_raw body retained for reference; record_fmt dispatches ───
inline result<knst_byte_string> _record_raw_legacy(const knst_audio_device* device,
                                                    uint32_t ms,
                                                    uint32_t rate,
                                                    uint32_t channels) noexcept {
    using R = result<knst_byte_string>;
    if (ms < 50 || ms > 60000 || rate < 8000 || rate > 192000 ||
        channels == 0 || channels > 8)
        return R::fail(status::InvalidArgument);
    if (device && !device->is_input) return R::fail(status::InvalidArgument);
#if !KNST_USING_PLATFORM_WINDOWS
    if (device && !knst_dev::is_safe_token(device->name, 200))
        return R::fail(status::InvalidArgument);
#endif

    const uint32_t total_frames = (uint32_t)((uint64_t)rate * ms / 1000);
    if (total_frames == 0) return R::fail(status::InvalidArgument);

#if defined(KNST_USING_PLATFORM_ANDROID)
    if (device) return R::fail(status::NotSupported);  // AAudio: default mic only
    return mic_aaudio::capture_s16le(rate, channels, total_frames);
#elif KNST_USING_PLATFORM_WINDOWS
    // Two capture paths on Windows:
    //   • waveIn  — default endpoint only (fast, no COM)
    //   • WASAPI  — ANY endpoint by id (COM, resolves via GetDevice)
    //
    // No explicit device → keep the cheap waveIn path.
    // Explicit device    → route through WASAPI so the named endpoint is
    //                      actually opened instead of silently using the
    //                      default mic.
    if (device) {
        capture_format cf{};
        cf.sample_rate = rate;
        cf.channels    = channels;
        cf.sample      = capture_format::sample_t::i16;
        cf.loopback    = false;
        return mic_wasapi::capture(cf, total_frames, device);
    }
    return mic_wavein::capture_s16le(rate, channels, total_frames);
#elif KNST_USING_PLATFORM_LINUX
    constexpr uint32_t k_warmup_ms = 80;
    const uint32_t total_ms = ms + k_warmup_ms;
    knst_dev::argv a;
    a << "-s" << "TERM" << knst_dev::format("%.2f", total_ms / 1000.0) << "parec";
    if (device) {
        knst_byte_string u8 = knst_dev::to_utf8(device->name);
        a << knst_dev::format("--device=%s", knst_dev::text(u8));
    }
    a << "--format=s16le"
      << knst_dev::format("--rate=%u", rate)
      << knst_dev::format("--channels=%u", channels)
      << "--latency-msec=20" << "--raw";
    auto out = knst_dev::run_codes("timeout", a, total_ms + 5000, { 124, 130, 143 });
    if (!out) return R::fail(out.st, out.native);
  
    const uint32_t want_bytes = (uint32_t)((uint64_t)ms * rate * channels * 2 / 1000);
    const uint32_t skip_bytes = (uint32_t)((uint64_t)k_warmup_ms * rate * channels * 2 / 1000);
    knst_byte_string result_buf;
    result_buf.reserve(want_bytes);
    if (out.value.length() > skip_bytes) {
        const uint32_t available = out.value.length() - skip_bytes;
        const uint32_t take      = available < want_bytes ? available : want_bytes;
        const unsigned char* src = out.value.data() + skip_bytes;
        for (uint32_t i = 0; i < take; ++i) result_buf.push_back(src[i]);
    }
    while (result_buf.length() < want_bytes) result_buf.push_back(0);
    return R::success(std::move(result_buf));
#else
    (void)device; (void)rate; (void)channels; (void)total_frames;
    return R::fail(status::NotSupported);
#endif
}

#if KNST_USING_PLATFORM_LINUX
namespace _parec_detail {
    inline const char* parec_fmt(capture_format::sample_t s) noexcept {
        switch (s) {
            case capture_format::sample_t::i24: return "s24le";
            case capture_format::sample_t::i32: return "s32le";
            case capture_format::sample_t::f32: return "float32le";
            case capture_format::sample_t::i16:
            default:                            return "s16le";
        }
    }
    inline uint32_t bytes_per_sample(capture_format::sample_t s) noexcept {
        switch (s) {
            case capture_format::sample_t::i24: return 3;
            case capture_format::sample_t::f32: return 4;
            case capture_format::sample_t::i32: return 4;
            case capture_format::sample_t::i16:
            default:                            return 2;
        }
    }
} // namespace _parec_detail

inline result<knst_byte_string> _record_parec_ext(
        const knst_audio_device* device, uint32_t ms,
        const capture_format& f, uint32_t total_frames) noexcept {
    using R = result<knst_byte_string>;
    constexpr uint32_t k_warmup_ms = 80;
    const uint32_t total_ms = ms + k_warmup_ms;
    const uint32_t bps = _parec_detail::bytes_per_sample(f.sample);
    const char* pfmt = _parec_detail::parec_fmt(f.sample);

    knst_dev::argv a;
    a << "-s" << "TERM" << knst_dev::format("%.2f", total_ms / 1000.0) << "parec";
    if (device) {
        knst_byte_string u8 = knst_dev::to_utf8(device->name);
        a << knst_dev::format("--device=%s", knst_dev::text(u8));
    }
    a << knst_dev::format("--format=%s", pfmt)
      << knst_dev::format("--rate=%u", f.sample_rate)
      << knst_dev::format("--channels=%u", f.channels)
      << "--latency-msec=20" << "--raw";
    auto out = knst_dev::run_codes("timeout", a, total_ms + 5000, { 124, 130, 143 });
    if (!out) return R::fail(out.st, out.native);

    const uint32_t want_bytes  = (uint32_t)((uint64_t)ms * f.sample_rate * f.channels * bps / 1000);
    const uint32_t skip_bytes  = (uint32_t)((uint64_t)k_warmup_ms * f.sample_rate * f.channels * bps / 1000);
    knst_byte_string result_buf;
    result_buf.reserve(want_bytes);
    if (out.value.length() > skip_bytes) {
        const uint32_t available = out.value.length() - skip_bytes;
        const uint32_t take      = available < want_bytes ? available : want_bytes;
        const unsigned char* src = out.value.data() + skip_bytes;
        for (uint32_t i = 0; i < take; ++i) result_buf.push_back(src[i]);
    }
    while (result_buf.length() < want_bytes) result_buf.push_back(0);
    return R::success(std::move(result_buf));
}
#endif // KNST_USING_PLATFORM_LINUX

// ─────────────────────────────────────────────────────────────────────────────
//  record_fmt — extended capture (multi-format + loopback)
// ─────────────────────────────────────────────────────────────────────────────
inline result<knst_byte_string> record_fmt(const knst_audio_device* device,
                                            uint32_t ms,
                                            const capture_format& f) noexcept {
    using R = result<knst_byte_string>;
        if (ms < 50 || ms > 60000) return R::fail(status::InvalidArgument);
    if (f.sample_rate < 8000 || f.sample_rate > 192000) return R::fail(status::InvalidArgument);
    if (f.channels == 0 || f.channels > 8) return R::fail(status::InvalidArgument);
    if (device && !f.loopback) {
        if (!device->is_input) return R::fail(status::InvalidArgument);
#if !KNST_USING_PLATFORM_WINDOWS
        // is_safe_token is a shell-injection guard for Linux (parec args).
        // Windows passes the WASAPI endpoint id straight to GetDevice();
        // those ids contain '{' / '}' which is_safe_token rejects.
        if (!knst_dev::is_safe_token(device->name, 200))
            return R::fail(status::InvalidArgument);
#endif
    }

    const uint32_t total_frames = (uint32_t)((uint64_t)f.sample_rate * ms / 1000);
    if (total_frames == 0) return R::fail(status::InvalidArgument);

    // Legacy path: i16 + non-loopback → cheap defaults.
    if (f.sample == capture_format::sample_t::i16 && !f.loopback) {
        return _record_raw_legacy(device, ms, f.sample_rate, f.channels);
    }

    // ── Platform-specific extended paths ──
#if defined(KNST_USING_PLATFORM_ANDROID)
    return mic_aaudio::capture_ext(f, total_frames, device);
#elif KNST_USING_PLATFORM_WINDOWS
    return mic_wasapi::capture(f, total_frames, device);
#elif KNST_USING_PLATFORM_LINUX
    // On Linux, loopback = capture a monitor source. The user must pass
    // a monitor device (from monitor_source()) — parec has no built-in
    // "default monitor". If no device is given, fail explicitly.
    if (f.loopback && !device) return R::fail(status::NotSupported);
    return _record_parec_ext(device, ms, f, total_frames);
#else
    (void)total_frames;
    return R::fail(status::NotSupported);
#endif
}

// Capture `seconds` seconds to a WAV file.
inline result<void> record_wav(const knst_audio_device* device,
                                const knst_c16string& path,
                                uint32_t seconds = 5,
                                uint32_t rate = 48000,
                                uint32_t channels = 1) noexcept {
    using R = result<void>;
    if (path.empty() || seconds == 0 || seconds > 3600 ||
        channels == 0 || channels > 8 ||
        rate < 8000 || rate > 192000)
        return R::fail(status::InvalidArgument);

    auto raw = record_raw(device, seconds * 1000u, rate, channels);
    if (!raw) return R::fail(raw.st, raw.native);

    knst_c16string abs = knst_file::resolve_path(path);
    if (!knst_file::ensure_parent_dir(abs)) return R::fail(status::IoError);

    unsigned char hdr[44];
    knst_aud::write_wav_header(hdr, (uint32_t)raw.value.length(), rate,
                                (uint16_t)channels, 16);
    knst_byte_string full;
    full.reserve(44 + (uint32_t)raw.value.length());
    for (int i = 0; i < 44; ++i) full.push_back(hdr[i]);
    for (uint32_t i = 0; i < raw.value.length(); ++i) full.push_back(raw.value[i]);
    if (!knst_file::write_file_data(abs, full.data(), full.length()))
        return R::fail(status::IoError);
    return R::success();
}

// Measure peak / RMS / clip for `ms` milliseconds.
inline result<knst_aud::level_info> measure_level(const knst_audio_device* device,
                                                    uint32_t ms = 500) noexcept {
    using R = result<knst_aud::level_info>;
    if (ms < 100 || ms > 5000) return R::fail(status::InvalidArgument);
    if (device && !device->is_input) return R::fail(status::InvalidArgument);
#if !KNST_USING_PLATFORM_WINDOWS
    if (device && !knst_dev::is_safe_token(device->name, 200))
        return R::fail(status::InvalidArgument);
#endif

    auto raw = record_raw(device, ms, 16000, 1);
    if (!raw) return R::fail(raw.st, raw.native);
    if (raw.value.empty()) return R::fail(status::IoError);

    const unsigned char* b = raw.value.data();
    const uint32_t n = raw.value.length() / 2;
    if (n == 0) return R::fail(status::IoError);

    double peak = 0, sum = 0;
    uint32_t clipped = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int16_t s = (int16_t)((uint16_t)b[i * 2] | ((uint16_t)b[i * 2 + 1] << 8));
        if (s == -32768 || s == 32767) ++clipped;
        double v = std::fabs(s / 32768.0);
        if (v > peak) peak = v;
        sum += v * v;
    }
    knst_aud::level_info r;
    r.peak    = peak;
    r.rms     = std::sqrt(sum / n);
    r.samples = n;
    r.clipped = clipped;
    r.peak_db = peak > 0 ? 20.0 * std::log10(peak) : -144.0;
    r.rms_db  = r.rms > 0 ? 20.0 * std::log10(r.rms) : -144.0;
    return R::success(std::move(r));
}

// Measure all sources (mono recording of each).
inline result<knst_vector<knst_aud::level_info>> measure_all(uint32_t ms = 300) noexcept {
    using R = result<knst_vector<knst_aud::level_info>>;
    auto srcs = knst_aud::audio::list_sources();
    if (!srcs) return R::fail(srcs.st, srcs.native);
    knst_vector<knst_aud::level_info> out;
    for (auto& s : srcs.value) {
        auto lv = measure_level(&s, ms);
        // PipeWire needs a short window to release the previous
        // parec device before the next capture can start. Without
        // it, back-to-back measure calls can intermittently fail.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (lv) {
            out.push_back(lv.value);
        } else {
            // Measurement failed (device busy, parec error, etc.).
            // Use -144 dB sentinel so callers can distinguish "silent"
            // from "couldn't measure" — default level_info() would
            // show 0 dB (full scale), which is misleading.
            knst_aud::level_info failed;
            failed.peak_db = -144.0;
            failed.rms_db  = -144.0;
            out.push_back(failed);
        }
    }
    return R::success(std::move(out));
}

// ============================================================
// Voice Activity Detection
// ============================================================

struct vad_config {
    uint32_t frame_ms         = 20;       // frame length in milliseconds
    float    energy_threshold = 0.01f;    // RMS threshold (0..1)
    float    zcr_max          = 0.3f;     // ZCR threshold (voiced ~0.05, unvoiced ~0.4)
    uint32_t hangover_frames  = 5;        // keep "active" for N extra frames
    uint32_t trigger_frames   = 3;        // need N consecutive active frames
    uint32_t sample_rate      = 16000;
};

class voice_detector {
public:
    explicit voice_detector(const vad_config& c = vad_config()) noexcept : cfg_(c) {}

    // Feed one block (mono, float). Returns true if voice is currently active.
    bool feed(const float* block, uint32_t n) noexcept {
        if (!block || n == 0) return false;

        // RMS energy
        double sum = 0;
        for (uint32_t i = 0; i < n; ++i) sum += block[i] * block[i];
        float rms = (float)std::sqrt(sum / n);

        // Zero-crossing rate
        uint32_t zc = 0;
        for (uint32_t i = 1; i < n; ++i) {
            bool a = block[i - 1] >= 0;
            bool b = block[i] >= 0;
            if (a != b) ++zc;
        }
        float zcr = n > 1 ? (float)zc / (float)(n - 1) : 0.0f;

        // Decision
        bool frame_active = (rms >= cfg_.energy_threshold) &&
                            (zcr  <= cfg_.zcr_max);

        if (frame_active) {
            ++active_run_;
            hangover_left_ = cfg_.hangover_frames;
        } else if (hangover_left_ > 0) {
            --hangover_left_;
            // Still "active" during hangover
        } else {
            active_run_ = 0;
        }

        bool triggered = (active_run_ >= cfg_.trigger_frames) ||
                         (hangover_left_ > 0);

        if (triggered && !was_active_) {
            was_active_ = true;
            last_edge_ = edge::start;
            return true;
        }
        if (!triggered && was_active_) {
            was_active_ = false;
            last_edge_ = edge::end;
            return false;
        }
        last_edge_ = edge::none;
        return triggered;
    }

    // Last state transition (for recording triggers)
    enum class edge : uint8_t { none, start, end };
    edge last_transition() const noexcept { return last_edge_; }

    bool is_active() const noexcept { return was_active_; }

private:
    vad_config cfg_;
    uint32_t   active_run_    = 0;
    uint32_t   hangover_left_ = 0;
    bool       was_active_    = false;
    edge       last_edge_     = edge::none;
};

// ============================================================
// Automatic Gain Control
// ============================================================

struct agc_config {
    float    target_peak     = 0.5f;
    float    min_gain        = 0.1f;
    float    max_gain        = 10.0f;
    float    attack_factor   = 0.3f;     // fast (when signal louder than target)
    float    release_factor  = 0.05f;    // slow (when signal quieter)
    float    noise_floor     = 0.001f;   // don't amplify below this
};

class agc {
public:
    explicit agc(const agc_config& c = agc_config()) noexcept : cfg_(c), gain_(1.0f) {}

    // Process block in place (mono). Returns gain applied.
    float process(float* block, uint32_t n) noexcept {
        if (!block || n == 0) return gain_;

        // Peak in current block
        float peak = 0;
        for (uint32_t i = 0; i < n; ++i) {
            float a = std::fabs(block[i]);
            if (a > peak) peak = a;
        }
        if (peak < cfg_.noise_floor) {
            // Below noise floor: don't increase gain (avoid boosting silence)
            for (uint32_t i = 0; i < n; ++i) block[i] *= gain_;
            return gain_;
        }

        float want = cfg_.target_peak / peak;
        if (want < cfg_.min_gain) want = cfg_.min_gain;
        if (want > cfg_.max_gain) want = cfg_.max_gain;

        float factor = (want < gain_) ? cfg_.attack_factor : cfg_.release_factor;
        gain_ = gain_ * (1.0f - factor) + want * factor;

        for (uint32_t i = 0; i < n; ++i) {
            float v = block[i] * gain_;
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            block[i] = v;
        }
        return gain_;
    }

    float current_gain() const noexcept { return gain_; }
    void  set_gain(float g) noexcept {
        if (g < cfg_.min_gain) g = cfg_.min_gain;
        if (g > cfg_.max_gain) g = cfg_.max_gain;
        gain_ = g;
    }
    void  reset() noexcept { gain_ = 1.0f; }

private:
    agc_config cfg_;
    float      gain_;
};

// ============================================================
// Noise gate
// ============================================================

struct gate_config {
    float    threshold_db    = -45.0f;
    float    attack_ms       = 5.0f;
    float    release_ms      = 100.0f;
    float    floor_db        = -80.0f;
    uint32_t sample_rate     = 16000;
};

class noise_gate {
public:
    explicit noise_gate(const gate_config& c = gate_config()) noexcept
        : cfg_(c), env_(0.0f) {
        attack_coef_  = coef_for(cfg_.attack_ms);
        release_coef_ = coef_for(cfg_.release_ms);
    }

    // Process block in place (mono).
    void process(float* block, uint32_t n) noexcept {
        if (!block || n == 0) return;
        float thresh = std::pow(10.0f, cfg_.threshold_db / 20.0f);
        float floor_g = std::pow(10.0f, cfg_.floor_db / 20.0f);

        for (uint32_t i = 0; i < n; ++i) {
            float a = std::fabs(block[i]);
            // Envelope follower
            float coef = (a > env_) ? attack_coef_ : release_coef_;
            env_ = env_ * (1.0f - coef) + a * coef;

            // Gain: 1.0 when env above threshold, ramp down to floor below
            float g;
            if (env_ >= thresh) {
                g = 1.0f;
            } else if (env_ <= thresh * 0.5f) {
                g = floor_g;
            } else {
                float t = (env_ - thresh * 0.5f) / (thresh * 0.5f);
                g = floor_g + (1.0f - floor_g) * t;
            }
            block[i] *= g;
        }
    }

    float envelope() const noexcept { return env_; }

private:
    gate_config cfg_;
    float       env_;
    float       attack_coef_;
    float       release_coef_;

    float coef_for(float ms) noexcept {
        if (ms <= 0.1f || cfg_.sample_rate == 0) return 1.0f;
        return 1.0f - std::exp(-1.0f / (ms * cfg_.sample_rate / 1000.0f));
    }
};

// ============================================================
// Pitch detection (autocorrelation)
// ============================================================

struct pitch_config {
    float    min_hz    = 80.0f;
    float    max_hz    = 800.0f;
    uint32_t sample_rate = 16000;
};

class pitch_detector {
public:
    explicit pitch_detector(const pitch_config& c = pitch_config()) noexcept : cfg_(c) {}

    // Detect fundamental frequency of a block (mono float). 0 if no pitch.
        float detect(const float* block, uint32_t n) noexcept {
        if (!block || n < 32) return 0.0f;

        uint32_t min_lag = (uint32_t)(cfg_.sample_rate / cfg_.max_hz);
        uint32_t max_lag = (uint32_t)(cfg_.sample_rate / cfg_.min_hz);
        if (max_lag >= n) max_lag = n - 1;
        if (min_lag >= max_lag || max_lag < min_lag + 2) return 0.0f;


        double energy = 0.0;
        for (uint32_t i = 0; i < n; ++i) energy += block[i] * block[i];
        if (energy < 1e-9) return 0.0f;

       
        knst_vector<float> corr;
        corr.resize(max_lag + 2);
        if (corr.size() != (uint32_t)(max_lag + 2)) return 0.0f;
        for (uint32_t lag = min_lag; lag <= max_lag; ++lag) {
            double sum = 0.0;
            for (uint32_t i = 0; i + lag < n; ++i)
                sum += block[i] * block[i + lag];
            corr[lag] = (float)(sum / energy);
        }

        
        float global_max = 0.0f;
        for (uint32_t lag = min_lag; lag <= max_lag; ++lag)
            if (corr[lag] > global_max) global_max = corr[lag];
        if (global_max < 0.001f) return 0.0f;


        const float threshold = 0.9f * global_max;
        uint32_t best_lag = 0;
        for (uint32_t lag = min_lag + 1; lag < max_lag; ++lag) {
            if (corr[lag] > corr[lag - 1] && corr[lag] >= corr[lag + 1] &&
                corr[lag] >= threshold) {
                best_lag = lag;
                break;
            }
        }
        if (best_lag == 0) {
     
            for (uint32_t lag = min_lag; lag <= max_lag; ++lag)
                if (corr[lag] == global_max) { best_lag = lag; break; }
            if (best_lag == 0) return 0.0f;
        }


        double refined = (double)best_lag;
        if (best_lag > min_lag && best_lag + 1 <= max_lag) {
            double a = corr[best_lag - 1];
            double b = corr[best_lag];
            double c = corr[best_lag + 1];
            double den = a - 2.0 * b + c;
            if (std::fabs(den) > 1e-12)
                refined += 0.5 * (a - c) / den;
        }

        return (float)(cfg_.sample_rate / refined);
    }

private:
    pitch_config cfg_;
};

// ============================================================
// Silence detector
// ============================================================

struct silence_config {
    float    threshold_db    = -50.0f;
    uint32_t min_silence_ms  = 500;
    uint32_t sample_rate     = 16000;
};

class silence_detector {
public:
    explicit silence_detector(const silence_config& c = silence_config()) noexcept : cfg_(c) {}

    // Feed a block. Returns true if silence duration threshold reached.
    bool feed(const float* block, uint32_t n) noexcept {
        if (!block || n == 0) return false;
        double sum = 0;
        for (uint32_t i = 0; i < n; ++i) sum += block[i] * block[i];
        float rms = (float)std::sqrt(sum / n);
        float rms_db = rms > 0 ? 20.0f * std::log10(rms) : -144.0f;

        if (rms_db < cfg_.threshold_db) {
            silent_samples_ += n;
        } else {
            silent_samples_ = 0;
        }

        uint32_t ms = cfg_.sample_rate ?
            (uint32_t)((uint64_t)silent_samples_ * 1000 / cfg_.sample_rate) : 0;
        return ms >= cfg_.min_silence_ms;
    }

    void reset() noexcept { silent_samples_ = 0; }

private:
    silence_config cfg_;
    uint64_t       silent_samples_ = 0;
};

// ============================================================
// Voice-triggered recorder
// ============================================================

class voice_recorder {
public:
    struct config {
        vad_config vad;
        knst_aud::wav::format format;
        uint32_t min_speech_ms    = 300;
        uint32_t max_silence_ms   = 1000;
        uint32_t pre_roll_ms      = 200;
    };

        explicit voice_recorder(const config& c) noexcept : cfg_(c) {}

    result<void> start(const knst_c16string& path) noexcept {
        using R = result<void>;
        if (path.empty()) return R::fail(status::InvalidArgument);
        // feed() receives mono blocks; pre-roll stores mono; capture_interleaved
        // multiplies by fmt_.channels. Require channels == 1 to avoid OOB.
        if (cfg_.format.channels != 1) return R::fail(status::NotSupported);
        auto r = recorder::open_file(path, cfg_.format);
        if (!r) return R::fail(r.st, r.native);
        rec_ = std::move(r.value);
        state_ = state::idle;
        speech_ms_ = 0;
        silence_ms_ = 0;
        init_pre_roll();
        return R::success();
    }

    // Feed one block (mono float). Returns bytes written on state transitions.
    // Returns 0 if not writing; returns >0 if writing resumed or continued.
    uint32_t feed(const float* block, uint32_t n) noexcept {
        if (state_ == state::stopped || !rec_.is_open()) return 0;

        bool active = vad_.feed(block, n);
        uint32_t block_ms = cfg_.format.rate ?
            (uint32_t)((uint64_t)n * 1000 / cfg_.format.rate) : 0;

        switch (state_) {
            case state::idle:
                if (active) {
                    // VAD triggered: first flush the pre-roll buffer, THEN live capture.
                    uint32_t pre_frames = flush_pre_roll();
                    auto r = rec_.capture_interleaved(block, n);
                    if (!r) return 0;
                    state_ = state::recording;
                    speech_ms_ = block_ms + frames_to_ms(pre_frames);
                    silence_ms_ = 0;
                    uint32_t bps = (uint32_t)knst_aud::sample_bytes(cfg_.format.sample);
                    return (pre_frames + n) * cfg_.format.channels * bps;
                }
                // Not triggered: keep feeding the ring buffer.
                add_to_pre_roll(block, n);
                return 0;

            case state::recording: {
                auto r = rec_.capture_interleaved(block, n);
                if (!r) return 0;
                if (active) {
                    speech_ms_ += block_ms;
                    silence_ms_ = 0;
                } else {
                    silence_ms_ += block_ms;
                    if (silence_ms_ >= cfg_.max_silence_ms &&
                        speech_ms_  >= cfg_.min_speech_ms) {
                        state_ = state::idle;
                        silence_ms_ = 0;
                        speech_ms_ = 0;
                        pre_ring_valid_ = 0;
                        pre_ring_write_pos_ = 0;
                    }
                }
                                return r.value * cfg_.format.channels *
                       (uint32_t)knst_aud::sample_bytes(cfg_.format.sample);
            }

            case state::stopped:
            default:
                return 0;
        }
    }

    result<void> stop() noexcept {
        using R = result<void>;
        state_ = state::stopped;
        if (rec_.is_open()) {
            return rec_.finish();
        }
        return R::success();
    }

    bool is_recording() const noexcept { return state_ == state::recording; }

private:
    enum class state : uint8_t { idle, recording, stopped };
    config             cfg_;
    voice_detector     vad_{};
    recorder           rec_;
    state              state_ = state::idle;
    uint32_t           speech_ms_  = 0;
    uint32_t           silence_ms_ = 0;

    // Pre-roll ring buffer (mono, chronological flush on VAD trigger).
    knst_vector<float> pre_ring_;
    uint32_t           pre_ring_capacity_  = 0;
    uint32_t           pre_ring_write_pos_ = 0;
    uint32_t           pre_ring_valid_     = 0;

    void init_pre_roll() noexcept {
        pre_ring_.clear();
        pre_ring_capacity_ = 0;
        pre_ring_write_pos_ = 0;
        pre_ring_valid_ = 0;
        if (cfg_.pre_roll_ms == 0 || cfg_.format.rate == 0) return;
        uint64_t cap = (uint64_t)cfg_.pre_roll_ms * cfg_.format.rate / 1000;
        if (cap == 0 || cap > 4800000) return;    // cap at 100s @ 48kHz
        pre_ring_capacity_ = (uint32_t)cap;
        pre_ring_.resize(pre_ring_capacity_);
        if (pre_ring_.size() != pre_ring_capacity_) pre_ring_capacity_ = 0;
    }

    void add_to_pre_roll(const float* block, uint32_t n) noexcept {
        if (pre_ring_capacity_ == 0 || !block) return;
        for (uint32_t i = 0; i < n; ++i) {
            pre_ring_[pre_ring_write_pos_] = block[i];
            pre_ring_write_pos_ = (pre_ring_write_pos_ + 1) % pre_ring_capacity_;
            if (pre_ring_valid_ < pre_ring_capacity_) pre_ring_valid_++;
        }
    }

        uint32_t flush_pre_roll() noexcept {
        if (pre_ring_valid_ == 0 || pre_ring_capacity_ == 0) return 0;
        uint32_t frames = pre_ring_valid_;
        uint32_t ch = cfg_.format.channels ? cfg_.format.channels : 1;
        uint64_t total = (uint64_t)frames * ch;
        if (total > 16u * 1024u * 1024u) return 0;   // sanity
        knst_vector<float> ordered;
        ordered.resize((uint32_t)total);
        if (ordered.size() != (uint32_t)total) return 0;

        if (pre_ring_valid_ < pre_ring_capacity_) {
            for (uint32_t i = 0; i < frames; ++i) {
                float v = pre_ring_[i];
                for (uint32_t c = 0; c < ch; ++c) ordered[i * ch + c] = v;
            }
        } else {
            for (uint32_t i = 0; i < frames; ++i) {
                uint32_t src = (pre_ring_write_pos_ + i) % pre_ring_capacity_;
                float v = pre_ring_[src];
                for (uint32_t c = 0; c < ch; ++c) ordered[i * ch + c] = v;
            }
        }
        auto r = rec_.capture_interleaved(ordered.data(), frames);
        pre_ring_valid_ = 0;
        pre_ring_write_pos_ = 0;
        return r ? frames : 0;
    }

    uint32_t frames_to_ms(uint32_t frames) const noexcept {
        if (cfg_.format.rate == 0) return 0;
        return (uint32_t)((uint64_t)frames * 1000 / cfg_.format.rate);
    }
};

// ============================================================
// Beamforming (delay-and-sum, 2-mic)
// ============================================================

// Estimate time delay (samples) between two mono blocks via cross-correlation.
inline int32_t estimate_delay(const float* a, const float* b,
                                uint32_t n, int32_t max_delay) noexcept {
    if (!a || !b || n < 32 || max_delay <= 0) return 0;
    if (max_delay >= (int32_t)n) max_delay = (int32_t)n - 1;
    int32_t best_d = 0;
    double best_corr = 0;
    for (int32_t d = -max_delay; d <= max_delay; ++d) {
        double sum = 0;
        uint32_t count = 0;
        for (uint32_t i = 0; i < n; ++i) {
            int32_t j = (int32_t)i + d;
            if (j < 0 || j >= (int32_t)n) continue;
            sum += a[i] * b[j];
            ++count;
        }
        if (count == 0) continue;
        double corr = sum / count;
        if (corr > best_corr) { best_corr = corr; best_d = d; }
    }
    return best_d;
}

// Delay-and-sum beamform two channels into one mono output.
inline knst_vector<float> beamform_2mic(const float* ch0, const float* ch1,
                                          uint32_t n, int32_t delay) noexcept {
    knst_vector<float> out;
    if (!ch0 || !ch1 || n == 0) return out;
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        int32_t j = (int32_t)i + delay;
        float a = ch0[i];
        float b = (j >= 0 && j < (int32_t)n) ? ch1[j] : 0.0f;
        out[i] = 0.5f * (a + b);
    }
    return out;
}

// ============================================================
// Microphone array
// ============================================================

class mic_array {
public:
    mic_array() noexcept = default;

    // Feed N channel blocks (interleaved or separate). Simplified: takes
    // interleaved mono mixture at sample_rate/channels.
    result<knst_vector<float>> beamform(const float* interleaved,
                                          uint32_t frames, uint32_t channels) noexcept {
        using R = result<knst_vector<float>>;
        if (!interleaved || frames == 0 || channels == 0)
            return R::fail(status::InvalidArgument);
        if (channels == 1) {
            knst_vector<float> out;
            out.resize(frames);
            ::memcpy(&out[0], interleaved, frames * sizeof(float));
            return R::success(std::move(out));
        }
        if (channels == 2) {
            // Simple 2-mic delay-and-sum
            knst_vector<float> c0, c1;
            c0.resize(frames);
            c1.resize(frames);
            for (uint32_t i = 0; i < frames; ++i) {
                c0[i] = interleaved[i * 2];
                c1[i] = interleaved[i * 2 + 1];
            }
            int32_t delay = estimate_delay(c0.data(), c1.data(), frames, 16);
            return R::success(beamform_2mic(c0.data(), c1.data(), frames, delay));
        }
        return R::fail(status::NotSupported);
    }
};

// ============================================================
// Format conversion helpers
// ============================================================

namespace fmt {

inline void s16_to_float(const int16_t* in, float* out, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) out[i] = in[i] / 32768.0f;
}

inline void float_to_s16(const float* in, int16_t* out, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) {
        float v = in[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = (int16_t)std::lround(v * 32767.0f);
    }
}

inline knst_vector<float> s16_bytes_to_float(const knst_byte_string& b) noexcept {
    knst_vector<float> out;
    uint32_t n = b.length() / 2;
    out.resize(n);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(b.data());
    for (uint32_t i = 0; i < n; ++i) {
        int16_t s = (int16_t)((uint16_t)p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8));
        out[i] = s / 32768.0f;
    }
    return out;
}

inline knst_byte_string float_to_s16_bytes(const float* f, uint32_t n) noexcept {
    knst_byte_string out;
    out.resize(n * 2);
    for (uint32_t i = 0; i < n; ++i) {
        float v = f[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t s = (int16_t)std::lround(v * 32767.0f);
        out[i * 2]     = (uint8_t)(s & 0xFF);
        out[i * 2 + 1] = (uint8_t)((s >> 8) & 0xFF);
    }
    return out;
}

} // namespace fmt

// ============================================================
// JSON bridge
// ============================================================

#if defined(KNST_JSON_HPP)

inline knst_json mic_refs_to_json(const knst_vector<mic_ref>& mics) noexcept {
    knst_json arr = knst_json::make_array();
    if (!arr.is_array()) return knst_json();
    for (auto& m : mics) {
        knst_json o = knst_json::make_object();
        if (!o.is_object()) continue;
        o.set("index",       knst_json((int64_t)m.index));
        o.set("name",        knst_json(m.name));
        o.set("description", knst_json(m.description));
        o.set("is_default",  knst_json(m.is_default));
        o.set("is_monitor",  knst_json(m.is_monitor));
        arr.push_back(std::move(o));
    }
    return arr;
}

#endif // KNST_JSON_HPP

} // namespace knst_mic