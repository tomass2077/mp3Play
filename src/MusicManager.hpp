#pragma once
#include "Arduino.h"
#include "Audio.h"
#include "SD_MMC.h"
#include "FS.h"
#include "pin_config.h"
#include "Mp3Meta.hpp"
#include <vector>
#include <string>

#define I2S_BCLK_PIN 17
#define I2S_LRC_PIN 15  // WARNING: pick a free GPIO; 17 conflicts with PIN_IIC_SCL
#define I2S_DOUT_PIN 21 // WARNING: 21 conflicts with PIN_TOUCH_RES

// SD card detect / hot-swap poll interval (ms)
#define SD_POLL_INTERVAL_MS 500

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct SongInfo
{
    std::string path;         // full SD path, e.g. "/Rock/song.mp3"
    std::string filename;     // bare filename, e.g. "song.mp3"
    std::string title;        // ID3 title  (may be empty)
    std::string artist;       // ID3 artist (may be empty)
    uint32_t durationSec = 0; // decoded duration in seconds (0 = unknown)
    uint32_t filesize = 0;    // bytes
};

struct FolderInfo
{
    std::string name; // folder name, e.g. "Rock"
    std::vector<SongInfo> songs;
};

struct PlayStats
{
    bool isPlaying = false;
    bool isPaused = false;
    bool isSdPresent = false;
    bool albumArtReady = false; // true when _albumArt holds valid art for currentPath
    uint32_t durationSec = 0;
    uint32_t positionSec = 0;
    uint32_t sampleRate = 0;
    uint8_t channels = 0;
    uint8_t bitsPerSample = 0;
    uint32_t bitRate = 0;
    char currentPath[128] = {};
    char title[64] = {};
    char artist[64] = {};
    uint8_t volume = 0; // 0..21
};

// ---------------------------------------------------------------------------

class MusicManager
{
public:
    MusicManager();

    // Call once from setup() — initialises I2S, starts SD polling.
    // SD mounting is handled internally via hot-swap logic.
    void begin();

    // Must be called every loop() iteration — NOT mutex-guarded (loop-core only).
    void loop();

    // -----------------------------------------------------------------------
    // Mutex-safe API — safe to call from any core / task
    // -----------------------------------------------------------------------

    void play(const char *path);
    void pause();
    void stop();

    void setVolume(uint8_t vol);
    uint8_t getVolume();

    // Returns a snapshot safe to read from the UI core.
    PlayStats getPlayStats();

    // Copy the current album art into `out`.
    // Returns false (and leaves `out` unchanged) when no art is loaded.
    bool getAlbumArt(AlbumArt &out);

    // Returns the cached library (scanned once per SD insertion).
    // Returns an empty vector if the card is absent.
    // Kicks off a fresh scan on the first call after a new insertion.
    std::vector<FolderInfo> scanLibrary();

    // True while the SD card is mounted and readable.
    bool isCardPresent();

    // -----------------------------------------------------------------------
    // ESP32-audioI2S callbacks (forwarded from free functions in .cpp)
    // -----------------------------------------------------------------------
    void onInfo(const char *info);
    void onId3(const char *info);
    void onEof(const char *info);

    // Returns true (and clears the flag) when a song finished naturally.
    // Call from the UI loop to trigger auto-advance.
    bool consumeEof();

    bool started = false; // set to true after begin()

private:
    // -----------------------------------------------------------------------
    // SD hot-swap helpers (all called only from loop-core)
    // -----------------------------------------------------------------------
    bool tryMountSd();
    void unmountSd();
    void pollSd(); // called every SD_POLL_INTERVAL_MS from loop()

    // -----------------------------------------------------------------------
    // Internal scan (loop-core, no mutex needed for SD access)
    // -----------------------------------------------------------------------
    std::vector<FolderInfo> scanLibraryInternal();

    Audio audio;

    SemaphoreHandle_t _mutex; // protects all shared state below

    // Pending command from UI core → consumed in loop()
    enum class Cmd
    {
        None,
        Play,
        Pause,
        Stop
    };
    Cmd _pendingCmd = Cmd::None;
    char _pendingPath[128] = {};
    uint8_t _pendingVolume = 255; // 255 = no change pending

    // SD state (loop-core writes, mutex for cross-core reads)
    bool _sdPresent = false;
    uint32_t _sdGeneration = 0; // bumps on every successful mount

    // Library cache
    bool _cacheDirty = true; // true = needs rescan
    uint32_t _cacheGeneration = 0;
    std::vector<FolderInfo> _libraryCache;

    // Playback state
    bool _playing = false;
    bool _paused = false;
    bool _eofPending = false; // set by onEof, consumed by consumeEof()
    char _currentPath[128] = {};
    char _title[64] = {};
    char _artist[64] = {};

    AlbumArt _albumArt;             // decoded 64x64 RGB565 cover art for current track
    bool _albumArtReady = false;    // true when _albumArt is valid for _currentPath
    char _pendingArtPath[128] = {}; // non-empty = art load deferred to next loop tick

    uint32_t _lastSdPollMs = 0;
};

extern MusicManager musicManager;
