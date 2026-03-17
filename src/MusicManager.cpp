#include "MusicManager.hpp"
#include "Mp3Meta.hpp"

// Global instance
MusicManager musicManager;

// ---- ESP32-audioI2S requires these as free functions ----
void audio_info(const char *info) { musicManager.onInfo(info); }
void audio_id3data(const char *info) { musicManager.onId3(info); }
void audio_eof_mp3(const char *info) { musicManager.onEof(info); }
// ---------------------------------------------------------

static bool isMp3(const char *name)
{
    size_t len = strlen(name);
    if (len < 4)
        return false;
    return strcasecmp(name + len - 4, ".mp3") == 0;
}

// ---------------------------------------------------------------------------

MusicManager::MusicManager()
{
    _mutex = xSemaphoreCreateMutex();
}

void MusicManager::begin()
{
    audio.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    audio.setVolume(12);

    // Attempt initial SD mount
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    if (tryMountSd())
        Serial.println("[sd] Initial mount OK");
    else
        Serial.println("[sd] No SD card at boot");

    started = true;
}

// ---------------------------------------------------------------------------
// SD hot-swap
// ---------------------------------------------------------------------------

bool MusicManager::tryMountSd()
{
    // Already mounted — nothing to do.
    if (_sdPresent)
        return true;

    if (!SD_MMC.begin("/sdcard", true, true))
        return false;

    if (SD_MMC.cardType() == CARD_NONE)
    {
        SD_MMC.end();
        return false;
    }

    _sdPresent = true;
    _sdGeneration++;
    _cacheDirty = true; // force rescan on next scanLibrary()

    Serial.printf("[sd] Mounted (gen %u)\n", _sdGeneration);
    return true;
}

void MusicManager::unmountSd()
{
    if (!_sdPresent)
        return;

    // Stop any active playback first
    audio.stopSong();
    SD_MMC.end();
    _sdPresent = false;

    // Invalidate cache
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _libraryCache.clear();
    _cacheDirty = true;
    _playing = false;
    _paused = false;
    _currentPath[0] = '\0';
    xSemaphoreGive(_mutex);

    Serial.println("[sd] Unmounted");
}

void MusicManager::pollSd()
{
    uint32_t now = millis();
    if (now - _lastSdPollMs < SD_POLL_INTERVAL_MS)
        return;
    _lastSdPollMs = now;

    if (_sdPresent)
    {
        // Check if card was removed — cardType() returns CARD_NONE when gone.
        if (SD_MMC.cardType() == CARD_NONE)
            unmountSd();
    }
    else
    {
        tryMountSd();
    }
}

// ---------------------------------------------------------------------------
// Main loop — runs on loop core
// ---------------------------------------------------------------------------

void MusicManager::loop()
{
    pollSd();

    Cmd cmd = Cmd::None;
    char path[128] = {};
    uint8_t vol = 255;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    cmd = _pendingCmd;
    _pendingCmd = Cmd::None;
    strlcpy(path, _pendingPath, sizeof(path));
    vol = _pendingVolume;
    _pendingVolume = 255;
    xSemaphoreGive(_mutex);

    if (vol != 255)
        audio.setVolume(vol);

    if (_sdPresent)
    {
        switch (cmd)
        {
        case Cmd::Play:
            audio.stopSong();
            audio.connecttoFS(SD_MMC, path);
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _playing = true;
            _paused = false;
            strlcpy(_currentPath, path, sizeof(_currentPath));
            _albumArtReady = false; // clear stale art immediately
            xSemaphoreGive(_mutex);
            // Defer art decode to next loop iteration so audio starts without delay
            strlcpy(_pendingArtPath, path, sizeof(_pendingArtPath));
            break;

        case Cmd::Pause:
            audio.pauseResume();
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _paused = !_paused;
            xSemaphoreGive(_mutex);
            break;

        case Cmd::Stop:
            audio.stopSong();
            _pendingArtPath[0] = '\0';
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _playing = false;
            _paused = false;
            _eofPending = false;
            _currentPath[0] = '\0';
            _albumArtReady = false;
            xSemaphoreGive(_mutex);
            break;

        default:
            break;
        }
    }

    audio.loop();

    // Deferred album art load — runs the tick *after* audio has started,
    // keeping the art decode off the critical path.  AlbumArt is heap-
    // allocated to avoid adding 8 KB to the loopTask stack.
    if (_pendingArtPath[0] != '\0' && _sdPresent)
    {
        char artPath[128];
        strlcpy(artPath, _pendingArtPath, sizeof(artPath));
        _pendingArtPath[0] = '\0';

        AlbumArt *art = new AlbumArt();
        if (art)
        {
            File mf = SD_MMC.open(artPath);
            if (mf)
            {
                readAlbumArt(mf, *art);
                mf.close();
            }
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _albumArt = *art;
            _albumArtReady = art->hasArt;
            xSemaphoreGive(_mutex);
            delete art;
        }
    }
}

// ---------------------------------------------------------------------------
// Mutex-safe public API
// ---------------------------------------------------------------------------

void MusicManager::play(const char *path)
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _pendingCmd = Cmd::Play;
    strlcpy(_pendingPath, path, sizeof(_pendingPath));
    xSemaphoreGive(_mutex);
}

void MusicManager::pause()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _pendingCmd = Cmd::Pause;
    xSemaphoreGive(_mutex);
}

void MusicManager::stop()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _pendingCmd = Cmd::Stop;
    xSemaphoreGive(_mutex);
}

void MusicManager::setVolume(uint8_t vol)
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _pendingVolume = vol;
    xSemaphoreGive(_mutex);
}

uint8_t MusicManager::getVolume()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint8_t v = audio.getVolume();
    xSemaphoreGive(_mutex);
    return v;
}

bool MusicManager::isCardPresent()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool p = _sdPresent;
    xSemaphoreGive(_mutex);
    return p;
}

PlayStats MusicManager::getPlayStats()
{
    PlayStats s;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    s.isPlaying = _playing;
    s.isPaused = _paused;
    s.isSdPresent = _sdPresent;
    s.albumArtReady = _albumArtReady;
    strlcpy(s.currentPath, _currentPath, sizeof(s.currentPath));
    strlcpy(s.title, _title, sizeof(s.title));
    strlcpy(s.artist, _artist, sizeof(s.artist));
    xSemaphoreGive(_mutex);

    s.durationSec = audio.getAudioFileDuration();
    s.positionSec = audio.getAudioCurrentTime();
    s.sampleRate = audio.getSampleRate();
    s.channels = audio.getChannels();
    s.bitsPerSample = audio.getBitsPerSample();
    s.bitRate = audio.getBitRate();
    s.volume = audio.getVolume();
    return s;
}

bool MusicManager::getAlbumArt(AlbumArt &out)
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ready = _albumArtReady;
    if (ready)
        out = _albumArt;
    xSemaphoreGive(_mutex);
    return ready;
}

// ---------------------------------------------------------------------------
// Library scan (runs on loop-core, no mutex needed for SD access)
// ---------------------------------------------------------------------------

std::vector<FolderInfo> MusicManager::scanLibraryInternal()
{
    std::vector<FolderInfo> library;
    if (!_sdPresent)
        return library;

    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory())
        return library;

    FolderInfo rootFolder;
    rootFolder.name = "/";

    File entry = root.openNextFile();
    while (entry)
    {
        if (entry.isDirectory())
        {
            FolderInfo folder;
            folder.name = entry.name();

            File dir = SD_MMC.open(String("/") + entry.name());
            if (dir)
            {
                File song = dir.openNextFile();
                while (song)
                {
                    if (!song.isDirectory() && isMp3(song.name()))
                    {
                        SongInfo si;
                        si.filename = song.name();
                        si.path = std::string("/") + folder.name + "/" + si.filename;
                        si.filesize = song.size();

                        // Read ID3 tags and duration
                        File mf = SD_MMC.open(si.path.c_str());
                        if (mf)
                        {
                            Mp3Meta meta;
                            readMp3Meta(mf, meta);
                            mf.close();
                            si.title = meta.title[0] ? meta.title : "";
                            si.artist = meta.artist[0] ? meta.artist : "";
                            si.durationSec = meta.durationSec;
                        }

                        folder.songs.push_back(si);
                    }
                    song.close();
                    song = dir.openNextFile();
                }
                dir.close();
            }

            if (!folder.songs.empty())
                library.push_back(folder);
        }
        else if (isMp3(entry.name()))
        {
            SongInfo si;
            si.filename = entry.name();
            si.path = std::string("/") + si.filename;
            si.filesize = entry.size();

            File mf = SD_MMC.open(si.path.c_str());
            if (mf)
            {
                Mp3Meta meta;
                readMp3Meta(mf, meta);
                mf.close();
                si.title = meta.title[0] ? meta.title : "";
                si.artist = meta.artist[0] ? meta.artist : "";
                si.durationSec = meta.durationSec;
            }

            rootFolder.songs.push_back(si);
        }

        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    library.insert(library.begin(), rootFolder);
    return library;
}

// Public cached version.
// NOTE: called from the UI task (Core 0), SD access must only happen on
// loop-core.  We solve this by scheduling the scan as a pending command and
// blocking until it is done — but that adds complexity.  A simpler and safe
// approach for this hardware: SD_MMC is NOT re-entrant from two cores at the
// same time, so we call this only from the loop-core (same as pollSd /
// audio.loop).  The UI task should call this once after `started` is set and
// then re-call whenever isCardPresent() flips.  The result is returned by
// value and is safe to read from any core afterwards.
std::vector<FolderInfo> MusicManager::scanLibrary()
{
    // If the cache is valid for the current SD generation, return it immediately.
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool dirty = _cacheDirty || (_cacheGeneration != _sdGeneration);
    bool present = _sdPresent;
    xSemaphoreGive(_mutex);

    if (!present)
    {
        // Card absent — return empty, cache already cleared by unmountSd()
        return {};
    }

    if (!dirty)
    {
        // Return a copy of the cache.
        xSemaphoreTake(_mutex, portMAX_DELAY);
        auto copy = _libraryCache;
        xSemaphoreGive(_mutex);
        return copy;
    }

    // Perform the scan (SD access — should be on loop-core).
    auto lib = scanLibraryInternal();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _libraryCache = lib;
    _cacheGeneration = _sdGeneration;
    _cacheDirty = false;
    xSemaphoreGive(_mutex);

    return lib;
}

// ---------------------------------------------------------------------------
// Audio callbacks (called from audio.loop() on loop-core)
// ---------------------------------------------------------------------------

void MusicManager::onInfo(const char *info)
{
    Serial.print("[audio] info: ");
    Serial.println(info);
}

void MusicManager::onId3(const char *info)
{
    Serial.print("[audio] id3:  ");
    Serial.println(info);

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (strncmp(info, "Title:", 6) == 0)
        strlcpy(_title, info + 6, sizeof(_title));
    else if (strncmp(info, "Artist:", 7) == 0)
        strlcpy(_artist, info + 7, sizeof(_artist));
    xSemaphoreGive(_mutex);
}

void MusicManager::onEof(const char *info)
{
    Serial.print("[audio] eof:  ");
    Serial.println(info);
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _playing = false;
    _paused = false;
    _eofPending = true;
    xSemaphoreGive(_mutex);
}

bool MusicManager::consumeEof()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool v = _eofPending;
    _eofPending = false;
    xSemaphoreGive(_mutex);
    return v;
}
