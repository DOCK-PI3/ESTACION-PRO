//  SPDX-License-Identifier: MIT
//
//  ESTACION-PRO Frontend
//  AudioManager.cpp
//
//  Low-level audio functions (using SDL2).
//

#include "AudioManager.h"

#include "Log.h"
#include "Settings.h"
#include "Sound.h"

#include <SDL2/SDL.h>

AudioManager::AudioManager() noexcept
{
    // Init on construction.
    init();
}

AudioManager::~AudioManager()
{
    // Deinit on destruction.
    deinit();
}

AudioManager& AudioManager::getInstance()
{
    static AudioManager instance;
    return instance;
}

void AudioManager::init()
{
    LOG(LogInfo) << "Setting up AudioManager...";

#if defined(__ANDROID__)
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        if (Settings::getInstance()->getString("AudioDriver") != "AAudio") {
            LOG(LogWarning) << "Requested OpenSL ES audio driver does not seem to be available, "
                               "reverting to AAudio";
            setenv("SDL_AUDIODRIVER", "AAudio", 1);
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                LOG(LogError) << "Couldn't initialize SDL audio: " << SDL_GetError();
                return;
            }
        }
        else {
            LOG(LogError) << "Couldn't initialize SDL audio: " << SDL_GetError();
            return;
        }
    }
#else
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        LOG(LogError) << "Couldn't initialize SDL audio: " << SDL_GetError();
        return;
    }
#endif

    LOG(LogInfo) << "Audio driver: " << SDL_GetCurrentAudioDriver();

    SDL_AudioSpec sRequestedAudioFormat;

    SDL_memset(&sRequestedAudioFormat, 0, sizeof(sRequestedAudioFormat));
    SDL_memset(&sAudioFormat, 0, sizeof(sAudioFormat));

    // Set up format and callback. SDL will negotiate these settings with the audio driver, so
    // if for instance the driver/hardware does not support 32-bit floating point output, 16-bit
    // integer may be selected instead. ES-DE will handle this automatically as there are no
    // hardcoded audio settings elsewhere in the code.
    sRequestedAudioFormat.freq = 44100;
    sRequestedAudioFormat.format = AUDIO_F32;
    sRequestedAudioFormat.channels = 2;
    sRequestedAudioFormat.samples = 1024;
    sRequestedAudioFormat.callback = mixAudio;
    sRequestedAudioFormat.userdata = nullptr;

    for (int i {0}; i < SDL_GetNumAudioDevices(0); ++i) {
        LOG(LogInfo) << "Detected playback device: " << SDL_GetAudioDeviceName(i, 0);
    }

    sAudioDevice = SDL_OpenAudioDevice(0, 0, &sRequestedAudioFormat, &sAudioFormat,
                                       SDL_AUDIO_ALLOW_ANY_CHANGE);

    if (sAudioDevice == 0) {
        LOG(LogError) << "Unable to open audio device: " << SDL_GetError();
        sHasAudioDevice = false;
    }

    if (sAudioFormat.freq != sRequestedAudioFormat.freq) {
        LOG(LogDebug) << "AudioManager::init(): Requested sample rate "
                      << std::to_string(sRequestedAudioFormat.freq)
                      << " could not be set, obtained " << std::to_string(sAudioFormat.freq);
    }
    if (sAudioFormat.format != sRequestedAudioFormat.format) {
        LOG(LogDebug) << "AudioManager::init(): Requested format "
                      << std::to_string(sRequestedAudioFormat.format)
                      << " could not be set, obtained " << std::to_string(sAudioFormat.format);
    }
    if (sAudioFormat.channels != sRequestedAudioFormat.channels) {
        LOG(LogDebug) << "AudioManager::init(): Requested channel count "
                      << std::to_string(sRequestedAudioFormat.channels)
                      << " could not be set, obtained " << std::to_string(sAudioFormat.channels);
    }
#if defined(_WIN64) || defined(__APPLE__)
    // Beats me why the buffer size is not divided by the channel count on some operating systems.
    if (sAudioFormat.samples != sRequestedAudioFormat.samples) {
#else
    if (sAudioFormat.samples != sRequestedAudioFormat.samples / sRequestedAudioFormat.channels) {
#endif
        LOG(LogDebug) << "AudioManager::init(): Requested sample buffer size "
                      << std::to_string(sRequestedAudioFormat.samples /
                                        sRequestedAudioFormat.channels)
                      << " could not be set, obtained " << std::to_string(sAudioFormat.samples);
    }

    // Just in case someone changed the es_settings.xml file manually to invalid values.
    if (Settings::getInstance()->getInt("SoundVolumeNavigation") > 100)
        Settings::getInstance()->setInt("SoundVolumeNavigation", 100);
    if (Settings::getInstance()->getInt("SoundVolumeNavigation") < 0)
        Settings::getInstance()->setInt("SoundVolumeNavigation", 0);
    if (Settings::getInstance()->getInt("SoundVolumeVideos") > 100)
        Settings::getInstance()->setInt("SoundVolumeVideos", 100);
    if (Settings::getInstance()->getInt("SoundVolumeVideos") < 0)
        Settings::getInstance()->setInt("SoundVolumeVideos", 0);
    if (Settings::getInstance()->getInt("SoundVolumeMusic") > 100)
        Settings::getInstance()->setInt("SoundVolumeMusic", 100);
    if (Settings::getInstance()->getInt("SoundVolumeMusic") < 0)
        Settings::getInstance()->setInt("SoundVolumeMusic", 0);

    // CRITICAL: After SDL_OpenAudioDevice negotiates with the driver, use sAudioFormat
    // (which contains the ACTUAL device parameters) not sRequestedAudioFormat.
    // This ensures the resampling/conversion rates match what the device actually supports.
    setupAudioStream(sAudioFormat.freq);
    setupMusicStream(sAudioFormat.freq);
}

void AudioManager::deinit()
{
    if (sAudioDevice == 0)
        return;

    SDL_LockAudioDevice(sAudioDevice);
    SDL_FreeAudioStream(sConversionStream);
    SDL_FreeAudioStream(sMusicStream);
    SDL_UnlockAudioDevice(sAudioDevice);

    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    sConversionStream = nullptr;
    sMusicStream = nullptr;
    sAudioDevice = 0;
}

void AudioManager::mixAudio(void* /*unused*/, Uint8* stream, int len)
{
    // Initialize the buffer to silence.
    SDL_memset(stream, 0, len);

    bool haveAudio {false};

    // ---------------------------------------------------------------
    // 1. Navigation / UI sounds.
    // ---------------------------------------------------------------
    for (auto& sound : sSoundVector) {
        if (sound->isPlaying()) {
            haveAudio = true;
            Uint32 restLength {sound->getLength() - sound->getPosition()};
            if (restLength > static_cast<Uint32>(len))
                restLength = len;
            SDL_MixAudioFormat(stream, &(sound->getData()[sound->getPosition()]),
                               sAudioFormat.format, restLength,
                               static_cast<int>(Settings::getInstance()->getInt(
                                                    "SoundVolumeNavigation") *
                                                1.28f));
            sound->setPosition(sound->getPosition() + restLength);
        }
    }

    // ---------------------------------------------------------------
    // 2. Video audio stream (VideoFFmpegComponent).
    // This mute flag is used to avoid playing buffered audio after a video is stopped.
    // ---------------------------------------------------------------
    int videoAvail {SDL_AudioStreamAvailable(sConversionStream)};
    if (videoAvail > 0) {
        haveAudio = true;
        int chunkLength {videoAvail > len ? len : videoAvail};
        std::vector<Uint8> converted(chunkLength);
        int processedLength {SDL_AudioStreamGet(sConversionStream,
                                                static_cast<void*>(&converted.at(0)),
                                                chunkLength)};
        if (processedLength > 0) {
            const int videoVol {sMuteStream ? 0 :
                                              static_cast<int>(Settings::getInstance()->getInt(
                                                                   "SoundVolumeVideos") *
                                                               1.28f)};
            SDL_MixAudioFormat(stream, &converted.at(0), sAudioFormat.format, processedLength,
                               videoVol);
        }
    }

    // ---------------------------------------------------------------
    // 3. Background music stream.
    // ---------------------------------------------------------------
    if (sMusicStream != nullptr) {
        int musicAvail {SDL_AudioStreamAvailable(sMusicStream)};
        if (musicAvail > 0) {
            haveAudio = true;
            int musicChunkLength {musicAvail > len ? len : musicAvail};
            std::vector<Uint8> musicConverted(musicChunkLength);
            int musicProcessed {SDL_AudioStreamGet(sMusicStream,
                                                   static_cast<void*>(&musicConverted.at(0)),
                                                   musicChunkLength)};
            if (musicProcessed > 0) {
                SDL_MixAudioFormat(stream, &musicConverted.at(0), sAudioFormat.format,
                                   musicProcessed,
                                   static_cast<int>(Settings::getInstance()->getInt(
                                                        "SoundVolumeMusic") *
                                                    1.28f));
            }
        }
    }

    // If no source has audio, pause the device until more data arrives.
    if (!haveAudio)
        SDL_PauseAudioDevice(sAudioDevice, 1);
}

void AudioManager::registerSound(std::shared_ptr<Sound> sound)
{
    // Add sound to sound vector.
    sSoundVector.push_back(sound);
}

void AudioManager::unregisterSound(std::shared_ptr<Sound> sound)
{
    for (unsigned int i {0}; i < sSoundVector.size(); ++i) {
        if (sSoundVector.at(i) == sound) {
            sSoundVector[i]->stop();
            sSoundVector.erase(sSoundVector.cbegin() + i);
            return;
        }
    }
}

void AudioManager::play()
{
    // Unpause audio, the mixer will figure out if samples need to be played...
    SDL_PauseAudioDevice(sAudioDevice, 0);
}

void AudioManager::stop()
{
    // Stop playing all Sounds.
    for (unsigned int i {0}; i < sSoundVector.size(); ++i) {
        if (sSoundVector.at(i)->isPlaying())
            sSoundVector[i]->stop();
    }
    // Pause audio.
    SDL_PauseAudioDevice(sAudioDevice, 1);
}

void AudioManager::setupAudioStream(int sampleRate)
{
    SDL_AudioStatus audioStatus {SDL_GetAudioDeviceStatus(sAudioDevice)};

    // It's very important to pause the audio device before setting up the stream,
    // or we may get random crashes if attempting to play samples at the same time.
    SDL_PauseAudioDevice(sAudioDevice, 1);
    SDL_FreeAudioStream(sConversionStream);

    // Used for streaming audio from videos.
    sConversionStream = SDL_NewAudioStream(AUDIO_F32, 2, sampleRate, sAudioFormat.format,
                                           sAudioFormat.channels, sAudioFormat.freq);
    if (sConversionStream == nullptr) {
        LOG(LogError) << "Failed to create audio conversion stream:";
        LOG(LogError) << SDL_GetError();
    }

    // If the device was previously in a playing state, then restore it.
    if (audioStatus == SDL_AUDIO_PLAYING)
        SDL_PauseAudioDevice(sAudioDevice, 0);
}

void AudioManager::processStream(const void* samples, unsigned count)
{
    SDL_LockAudioDevice(sAudioDevice);

    if (SDL_AudioStreamPut(sConversionStream, samples, count * sizeof(Uint8)) == -1) {
        LOG(LogError) << "Failed to put samples in the conversion stream:";
        LOG(LogError) << SDL_GetError();
        SDL_UnlockAudioDevice(sAudioDevice);
        return;
    }

    if (count > 0)
        SDL_PauseAudioDevice(sAudioDevice, 0);

    SDL_UnlockAudioDevice(sAudioDevice);
}

void AudioManager::clearStream()
{
    SDL_LockAudioDevice(sAudioDevice);
    SDL_AudioStreamClear(sConversionStream);
    SDL_UnlockAudioDevice(sAudioDevice);
}

void AudioManager::setupMusicStream(int sampleRate)
{
    SDL_LockAudioDevice(sAudioDevice);
    SDL_FreeAudioStream(sMusicStream);

    // Background music stream: accepts AUDIO_F32 stereo at file sample rate and converts
    // to the audio device's output format.
    sMusicStream = SDL_NewAudioStream(AUDIO_F32, 2, sampleRate, sAudioFormat.format,
                                      sAudioFormat.channels, sAudioFormat.freq);
    if (sMusicStream == nullptr) {
        LOG(LogError) << "Failed to create music audio stream:";
        LOG(LogError) << SDL_GetError();
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

void AudioManager::processMusicStream(const void* samples, unsigned count)
{
    if (sAudioDevice == 0 || sMusicStream == nullptr)
        return;

    SDL_LockAudioDevice(sAudioDevice);
    if (SDL_AudioStreamPut(sMusicStream, samples, static_cast<int>(count)) == -1) {
        LOG(LogError) << "Failed to put samples in the music stream:";
        LOG(LogError) << SDL_GetError();
        SDL_UnlockAudioDevice(sAudioDevice);
        return;
    }
    if (count > 0)
        SDL_PauseAudioDevice(sAudioDevice, 0);
    SDL_UnlockAudioDevice(sAudioDevice);
}

void AudioManager::clearMusicStream()
{
    if (sAudioDevice == 0 || sMusicStream == nullptr)
        return;
    SDL_LockAudioDevice(sAudioDevice);
    SDL_AudioStreamClear(sMusicStream);
    SDL_UnlockAudioDevice(sAudioDevice);
}
