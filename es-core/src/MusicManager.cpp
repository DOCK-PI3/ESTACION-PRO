//  SPDX-License-Identifier: MIT
//
//  ESTACION-PRO Frontend
//  MusicManager.cpp
//
//  Background music player.
//

#include "MusicManager.h"

#include "AudioManager.h"
#include "Log.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"

#include <SDL2/SDL_audio.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

MusicManager::~MusicManager()
{
    deinit();
}

MusicManager& MusicManager::getInstance()
{
    static MusicManager instance;
    return instance;
}

void MusicManager::init()
{
    mEnabled = Settings::getInstance()->getBool("BackgroundMusic");
    if (mEnabled)
        startPlayback();
}

void MusicManager::deinit()
{
    stopPlayback();
}

void MusicManager::startPlayback()
{
    if (mRunning)
        return;

    scanMusicDirectory();
    if (mTrackList.empty()) {
        LOG(LogInfo) << "MusicManager: No tracks found in musica directory, skipping playback";
        return;
    }

    mRunning = true;
    mPaused = false;
    mIsPlaying = true;

    // The music stream is already initialized by AudioManager during its init() phase.
    // We just start the decoder thread here.
    mDecoderThread = std::thread(&MusicManager::decoderThreadFunc, this);
    LOG(LogInfo) << "MusicManager: Started background music playback";
}

void MusicManager::stopPlayback()
{
    if (!mRunning)
        return;

    mRunning = false;
    mPaused = false; // Wake the thread if it is sleeping on pause.
    if (mDecoderThread.joinable())
        mDecoderThread.join();

    mIsPlaying = false;
    AudioManager::clearMusicStream();
    LOG(LogInfo) << "MusicManager: Stopped background music playback";
}

void MusicManager::pausePlayback()
{
    mPaused = true;
    AudioManager::clearMusicStream();
}

void MusicManager::resumePlayback()
{
    if (!mEnabled)
        return;
    if (!mRunning) {
        // Thread was stopped (e.g. after DEINIT_ON_LAUNCH), restart it.
        startPlayback();
        return;
    }
    mPaused = false;
}

void MusicManager::scanMusicDirectory()
{
    mTrackList.clear();
    const std::string musicDir {Utils::FileSystem::getAppDataDirectory() + "/musica"};

    if (!Utils::FileSystem::isDirectory(musicDir))
        return;

    static const std::vector<std::string> supportedExts {
        ".mp3", ".ogg", ".wav", ".flac", ".aac", ".m4a", ".opus"};

    for (const std::string& file : Utils::FileSystem::getDirContent(musicDir)) {
        if (!Utils::FileSystem::isRegularFile(file))
            continue;
        std::string ext {Utils::FileSystem::getExtension(file)};
        // Case-insensitive comparison.
        std::string extLower {ext};
        for (char& c : extLower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const std::string& supported : supportedExts) {
            if (extLower == supported) {
                mTrackList.push_back(file);
                break;
            }
        }
    }

    LOG(LogInfo) << "MusicManager: Found " << mTrackList.size()
                 << " track(s) in " << musicDir;
}

std::string MusicManager::pickNextTrack()
{
    if (mTrackList.empty())
        return "";
    if (mTrackList.size() == 1)
        return mTrackList.front();

    static std::mt19937 rng {std::random_device {}()};
    std::uniform_int_distribution<size_t> dist {0, mTrackList.size() - 1};

    std::string next;
    int attempts {0};
    do {
        next = mTrackList[dist(rng)];
        ++attempts;
    } while (next == mLastTrack && attempts < 10);

    return next;
}

void MusicManager::cleanupFFmpeg()
{
    if (mSwrCtx) {
        swr_free(&mSwrCtx);
        mSwrCtx = nullptr;
    }
    if (mFrameResampled) {
        av_frame_free(&mFrameResampled);
        mFrameResampled = nullptr;
    }
    if (mFrame) {
        av_frame_free(&mFrame);
        mFrame = nullptr;
    }
    if (mPacket) {
        av_packet_free(&mPacket);
        mPacket = nullptr;
    }
    if (mCodecCtx) {
        avcodec_free_context(&mCodecCtx);
        mCodecCtx = nullptr;
    }
    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
        mFormatCtx = nullptr;
    }
    mAudioStreamIndex = -1;
}

void MusicManager::decoderThreadFunc()
{
    // Silence FFmpeg log output from decoder thread.
    av_log_set_callback(nullptr);

    while (mRunning) {
        // Handle pause at the top of the track loop.
        if (mPaused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        const std::string trackPath {pickNextTrack()};
        if (trackPath.empty()) {
            mRunning = false;
            break;
        }

        mLastTrack = trackPath;
        LOG(LogInfo) << "MusicManager: Playing \"" << trackPath << "\"";

        // ---------------------------------------------------------------
        // Open the audio file with FFmpeg.
        // ---------------------------------------------------------------
        const std::string filePath {"file:" + trackPath};
        if (avformat_open_input(&mFormatCtx, filePath.c_str(), nullptr, nullptr) < 0) {
            LOG(LogError) << "MusicManager: Couldn't open \"" << trackPath << "\"";
            cleanupFFmpeg();
            continue;
        }

        if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
            LOG(LogError) << "MusicManager: Couldn't find stream info for \"" << trackPath << "\"";
            cleanupFFmpeg();
            continue;
        }

        // ---------------------------------------------------------------
        // Find the best audio stream and open its codec.
        // ---------------------------------------------------------------
        const AVCodec* codec {nullptr};
#if LIBAVUTIL_VERSION_MAJOR >= 57
        mAudioStreamIndex =
            av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1,
                                const_cast<const AVCodec**>(&codec), 0);
#else
        mAudioStreamIndex =
            av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
#endif

        if (mAudioStreamIndex < 0 || codec == nullptr) {
            LOG(LogError) << "MusicManager: No audio stream found in \"" << trackPath << "\"";
            cleanupFFmpeg();
            continue;
        }

        mCodecCtx = avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            cleanupFFmpeg();
            continue;
        }

        if (avcodec_parameters_to_context(
                mCodecCtx, mFormatCtx->streams[mAudioStreamIndex]->codecpar) < 0) {
            cleanupFFmpeg();
            continue;
        }

        if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            LOG(LogError) << "MusicManager: Couldn't open codec for \"" << trackPath << "\"";
            cleanupFFmpeg();
            continue;
        }

        // ---------------------------------------------------------------
        // Set up SwrContext: decode any format → F32 stereo at device rate.
        // ---------------------------------------------------------------
        const int outFreq {AudioManager::sAudioFormat.freq > 0 ? AudioManager::sAudioFormat.freq :
                                                                  44100};
        const AVSampleFormat outFmt {AV_SAMPLE_FMT_FLT};

        mSwrCtx = swr_alloc();
        if (!mSwrCtx) {
            cleanupFFmpeg();
            continue;
        }

#if LIBAVUTIL_VERSION_MAJOR >= 58 ||                                                               \
    (LIBAVUTIL_VERSION_MAJOR >= 57 && LIBAVUTIL_VERSION_MINOR >= 28)
        // FFmpeg 5.1+ channel layout API.
        AVChannelLayout outLayout {};
        av_channel_layout_from_mask(&outLayout, AV_CH_LAYOUT_STEREO);
        AVChannelLayout inLayout {};
        av_channel_layout_copy(&inLayout, &mCodecCtx->ch_layout);
        if (swr_alloc_set_opts2(&mSwrCtx, &outLayout, outFmt, outFreq, &inLayout,
                                mCodecCtx->sample_fmt, mCodecCtx->sample_rate, 0,
                                nullptr) < 0) {
            av_channel_layout_uninit(&inLayout);
            LOG(LogError) << "MusicManager: swr_alloc_set_opts2 failed for \"" << trackPath
                          << "\"";
            cleanupFFmpeg();
            continue;
        }
        av_channel_layout_uninit(&inLayout);
#else
        // Older FFmpeg channel layout API.
        const int64_t inLayout {mCodecCtx->channel_layout
                                     ? static_cast<int64_t>(mCodecCtx->channel_layout)
                                     : av_get_default_channel_layout(mCodecCtx->channels)};
        av_opt_set_int(mSwrCtx, "in_channel_layout", inLayout, 0);
        av_opt_set_int(mSwrCtx, "in_sample_rate", mCodecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(mSwrCtx, "in_sample_fmt", mCodecCtx->sample_fmt, 0);
        av_opt_set_int(mSwrCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        av_opt_set_int(mSwrCtx, "out_sample_rate", outFreq, 0);
        av_opt_set_sample_fmt(mSwrCtx, "out_sample_fmt", outFmt, 0);
#endif

        if (swr_init(mSwrCtx) < 0) {
            LOG(LogError) << "MusicManager: swr_init failed for \"" << trackPath << "\"";
            cleanupFFmpeg();
            continue;
        }

        // ---------------------------------------------------------------
        // Allocate frames and packet.
        // ---------------------------------------------------------------
        mPacket = av_packet_alloc();
        mFrame = av_frame_alloc();
        mFrameResampled = av_frame_alloc();

        if (!mPacket || !mFrame || !mFrameResampled) {
            cleanupFFmpeg();
            continue;
        }

        // Pre-fill destination frame format so swr_convert_frame can allocate its buffer.
        mFrameResampled->format = outFmt;
        mFrameResampled->sample_rate = outFreq;
#if LIBAVUTIL_VERSION_MAJOR >= 58 ||                                                               \
    (LIBAVUTIL_VERSION_MAJOR >= 57 && LIBAVUTIL_VERSION_MINOR >= 28)
        av_channel_layout_from_mask(&mFrameResampled->ch_layout, AV_CH_LAYOUT_STEREO);
#else
        mFrameResampled->channel_layout = AV_CH_LAYOUT_STEREO;
        mFrameResampled->channels = 2;
#endif

        // ---------------------------------------------------------------
        // Decode loop for the current track.
        // ---------------------------------------------------------------
        bool trackDone {false};
        while (mRunning && !trackDone) {
            if (mPaused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            // Throttle: keep at most ~1 second buffered to avoid unbounded memory growth.
            if (AudioManager::sMusicStream != nullptr) {
                const int deviceFreq {AudioManager::sAudioFormat.freq > 0 ?
                                          AudioManager::sAudioFormat.freq :
                                          44100};
                const int bytesPerSec {deviceFreq * 2 * static_cast<int>(sizeof(float))};
                while (mRunning && !mPaused &&
                       SDL_AudioStreamAvailable(AudioManager::sMusicStream) >= bytesPerSec) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            if (!mRunning || mPaused)
                continue;

            const int readRet {av_read_frame(mFormatCtx, mPacket)};
            if (readRet == AVERROR_EOF || readRet < 0) {
                trackDone = true;
                break;
            }

            if (mPacket->stream_index != mAudioStreamIndex) {
                av_packet_unref(mPacket);
                continue;
            }

            if (avcodec_send_packet(mCodecCtx, mPacket) < 0) {
                av_packet_unref(mPacket);
                continue;
            }
            av_packet_unref(mPacket);

            // Drain all decoded frames from the packet.
            while (mRunning && !mPaused) {
                const int recvRet {avcodec_receive_frame(mCodecCtx, mFrame)};
                if (recvRet == AVERROR(EAGAIN) || recvRet == AVERROR_EOF)
                    break;
                if (recvRet < 0)
                    break;

                // Convert to F32 stereo at device sample rate.
                av_frame_unref(mFrameResampled);
                mFrameResampled->format = outFmt;
                mFrameResampled->sample_rate = outFreq;
#if LIBAVUTIL_VERSION_MAJOR >= 58 ||                                                               \
    (LIBAVUTIL_VERSION_MAJOR >= 57 && LIBAVUTIL_VERSION_MINOR >= 28)
                av_channel_layout_from_mask(&mFrameResampled->ch_layout, AV_CH_LAYOUT_STEREO);
#else
                mFrameResampled->channel_layout = AV_CH_LAYOUT_STEREO;
                mFrameResampled->channels = 2;
#endif

                if (swr_convert_frame(mSwrCtx, mFrameResampled, mFrame) < 0) {
                    av_frame_unref(mFrame);
                    continue;
                }

                const int bufferSize {mFrameResampled->nb_samples * 2 *
                                      static_cast<int>(sizeof(float))};
                if (bufferSize > 0) {
                    AudioManager::processMusicStream(mFrameResampled->data[0],
                                                    static_cast<unsigned>(bufferSize));
                }

                av_frame_unref(mFrame);
            }
        }

        // Flush any remaining resampled samples.
        if (!mPaused && mSwrCtx) {
            av_frame_unref(mFrameResampled);
            mFrameResampled->format = outFmt;
            mFrameResampled->sample_rate = outFreq;
#if LIBAVUTIL_VERSION_MAJOR >= 58 ||                                                               \
    (LIBAVUTIL_VERSION_MAJOR >= 57 && LIBAVUTIL_VERSION_MINOR >= 28)
            av_channel_layout_from_mask(&mFrameResampled->ch_layout, AV_CH_LAYOUT_STEREO);
#else
            mFrameResampled->channel_layout = AV_CH_LAYOUT_STEREO;
            mFrameResampled->channels = 2;
#endif
            if (swr_convert_frame(mSwrCtx, mFrameResampled, nullptr) >= 0) {
                const int bufferSize {mFrameResampled->nb_samples * 2 *
                                      static_cast<int>(sizeof(float))};
                if (bufferSize > 0) {
                    AudioManager::processMusicStream(mFrameResampled->data[0],
                                                    static_cast<unsigned>(bufferSize));
                }
            }
        }

        cleanupFFmpeg();

        // Short pause between tracks.
        if (mRunning && !mPaused)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    mIsPlaying = false;
    LOG(LogInfo) << "MusicManager: Decoder thread exiting";
}
