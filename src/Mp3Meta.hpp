#pragma once
// Lightweight MP3 metadata reader (ID3v2 + ID3v1) and duration estimator.
// Only reads the first few KB of the file — no full decode required.
// Works entirely from a File handle (SD / SPIFFS / etc.)

#include "FS.h"
#include <stdint.h>
#include <string.h>

struct Mp3Meta
{
    char title[64] = {};
    char artist[64] = {};
    uint32_t durationSec = 0; // 0 = unknown / parse failed
};

// 64×64 RGB565 album art buffer (little-endian, row-major).
// `hasArt` is false when no APIC frame was found or decoding failed.
struct AlbumArt
{
    static constexpr int W = 64;
    static constexpr int H = 64;
    uint16_t pixels[W * H] = {}; // RGB565, row-major
    bool hasArt = false;
};

// Fill `out` with metadata read from `f`.
// The file position may be modified; caller should not rely on it.
void readMp3Meta(File &f, Mp3Meta &out);

// Extract the first APIC (cover art) frame from `f` and decode it
// into a 64×64 RGB565 buffer.  Only JPEG images are supported.
// Returns true on success; `out.hasArt` is set accordingly.
bool readAlbumArt(File &f, AlbumArt &out);
