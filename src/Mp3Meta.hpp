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

// Fill `out` with metadata read from `f`.
// The file position may be modified; caller should not rely on it.
void readMp3Meta(File &f, Mp3Meta &out);
