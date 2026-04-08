//  SPDX-License-Identifier: MIT
//
//  ESTACION-PRO Frontend
//  MusicManager.h
//
//  Background music player.
//  Decodes audio files from ~/ES-PRO/musica using FFmpeg and feeds them
//  through AudioManager's dedicated music stream (SDL2 audio device).
//

#ifndef ES_CORE_MUSIC_MANAGER_H
#define ES_CORE_MUSIC_MANAGER_H

#include <atomic>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

class MusicManager
{
public:
    static MusicManager& getInstance();
    ~MusicManager();

    // Initialize: reads settings and starts playback if enabled.
    void init();
    // Stop playback and clean up.
    void deinit();

    // Start the decoder thread and begin playing tracks.
    void startPlayback();
    // Signal the decoder thread to stop and wait for it to finish.
    void stopPlayback();

    // Pause/resume without stopping the thread (for non-DEINIT_ON_LAUNCH case).
    void pausePlayback();
    void resumePlayback();

    bool isEnabled() const { return mEnabled; }
    bool isPlaying() const { return mIsPlaying.load(); }
    void setEnabled(bool enabled) { mEnabled = enabled; }

private:
    MusicManager() = default;
    MusicManager(const MusicManager&) = delete;
    MusicManager& operator=(const MusicManager&) = delete;

    void scanMusicDirectory();
    std::string pickNextTrack();
    void decoderThreadFunc();
    void cleanupFFmpeg();

    std::vector<std::string> mTrackList;
    std::string mLastTrack;

    std::thread mDecoderThread;
    std::atomic<bool> mRunning {false};
    std::atomic<bool> mPaused {false};
    std::atomic<bool> mIsPlaying {false};
    bool mEnabled {false};

    // FFmpeg state – only accessed from the decoder thread.
    AVFormatContext* mFormatCtx {nullptr};
    AVCodecContext* mCodecCtx {nullptr};
    SwrContext* mSwrCtx {nullptr};
    AVPacket* mPacket {nullptr};
    AVFrame* mFrame {nullptr};
    AVFrame* mFrameResampled {nullptr};
    int mAudioStreamIndex {-1};
};

#endif // ES_CORE_MUSIC_MANAGER_H
