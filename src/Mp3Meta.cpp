#include "Mp3Meta.hpp"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static uint32_t readU32BE(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t readU32LE(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | p[0];
}

// ID3v2 syncsafe integer (7 bits per byte, MSB first)
static uint32_t syncsafe(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7F) << 21) |
           ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) |
           (uint32_t)(p[3] & 0x7F);
}

// Encode a Unicode codepoint as UTF-8 into dst[di], advancing di.
// Returns false if there is not enough space (needs up to 3 bytes + null).
static bool utf8Encode(char *dst, size_t &di, size_t maxLen, uint32_t cp)
{
    if (cp == 0)
        return true; // skip null codepoints
    if (cp < 0x80)
    {
        if (di + 1 >= maxLen)
            return false;
        dst[di++] = (char)cp;
    }
    else if (cp < 0x800)
    {
        if (di + 2 >= maxLen)
            return false;
        dst[di++] = (char)(0xC0 | (cp >> 6));
        dst[di++] = (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        if (di + 3 >= maxLen)
            return false;
        dst[di++] = (char)(0xE0 | (cp >> 12));
        dst[di++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[di++] = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        // 4-byte UTF-8 (supplementary planes — rare in tags)
        if (di + 4 >= maxLen)
            return false;
        dst[di++] = (char)(0xF0 | (cp >> 18));
        dst[di++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[di++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[di++] = (char)(0x80 | (cp & 0x3F));
    }
    return true;
}

// Copy an ID3 text frame payload into dst as UTF-8.
// src[0] is the ID3v2 encoding byte; the rest is the text.
// ISO-8859-1 bytes 0x80-0xFF map 1:1 to Unicode U+0080-U+00FF (→ 2-byte UTF-8).
// UTF-16 LE/BE surrogate pairs are handled for full BMP + supplementary coverage.
static void copyTag(char *dst, const uint8_t *src, size_t srcLen, size_t maxLen)
{
    if (srcLen == 0 || maxLen == 0)
    {
        dst[0] = '\0';
        return;
    }

    // ID3v2 text frames start with an encoding byte:
    //   0x00 = ISO-8859-1  0x01 = UTF-16 with BOM  0x02 = UTF-16BE  0x03 = UTF-8
    uint8_t enc = src[0];
    const uint8_t *text = src + 1;
    size_t textLen = srcLen - 1;
    size_t di = 0;

    if (enc == 0x01 || enc == 0x02)
    {
        // --- UTF-16 → UTF-8 ---
        // Determine byte order from BOM (enc==0x01) or default to BE (enc==0x02)
        bool littleEndian = (enc == 0x01); // default for enc==0x01 before BOM check
        size_t i = 0;
        if (textLen >= 2)
        {
            if (text[0] == 0xFF && text[1] == 0xFE)
            {
                littleEndian = true;
                i = 2;
            }
            else if (text[0] == 0xFE && text[1] == 0xFF)
            {
                littleEndian = false;
                i = 2;
            }
            // No BOM: enc==0x02 means BE, enc==0x01 is ambiguous — assume LE
        }

        while (i + 1 < textLen)
        {
            uint16_t w1 = littleEndian
                              ? ((uint16_t)text[i] | ((uint16_t)text[i + 1] << 8))
                              : ((uint16_t)(text[i] << 8) | text[i + 1]);
            i += 2;
            if (w1 == 0)
                break; // null terminator

            uint32_t cp;
            if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 1 < textLen)
            {
                // High surrogate — read low surrogate
                uint16_t w2 = littleEndian
                                  ? ((uint16_t)text[i] | ((uint16_t)text[i + 1] << 8))
                                  : ((uint16_t)(text[i] << 8) | text[i + 1]);
                if (w2 >= 0xDC00 && w2 <= 0xDFFF)
                {
                    i += 2;
                    cp = 0x10000 + (((uint32_t)(w1 - 0xD800)) << 10) + (w2 - 0xDC00);
                }
                else
                {
                    cp = '?'; // unpaired surrogate
                }
            }
            else if (w1 >= 0xDC00 && w1 <= 0xDFFF)
            {
                cp = '?'; // stray low surrogate
            }
            else
            {
                cp = w1;
            }

            if (!utf8Encode(dst, di, maxLen, cp))
                break;
        }
        dst[di] = '\0';
        return;
    }

    if (enc == 0x03)
    {
        // --- UTF-8 — copy verbatim (already valid UTF-8) ---
        for (size_t i = 0; i < textLen && di < maxLen - 1; ++i)
        {
            if (text[i] == 0)
                break;
            dst[di++] = (char)text[i];
        }
        dst[di] = '\0';
        return;
    }

    // --- enc == 0x00: ISO-8859-1 → UTF-8 ---
    // U+0000–U+007F: 1 byte; U+0080–U+00FF: 2 bytes (direct mapping)
    for (size_t i = 0; i < textLen; ++i)
    {
        uint8_t c = text[i];
        if (c == 0)
            break;
        if (!utf8Encode(dst, di, maxLen, (uint32_t)c))
            break;
    }
    dst[di] = '\0';
}

// ---------------------------------------------------------------------------
// ID3v2 tag parser
// Returns the total ID3v2 tag size (including 10-byte header) or 0 if absent.
// Fills title/artist from TIT2/TPE1 frames.
// ---------------------------------------------------------------------------

static uint32_t parseId3v2(File &f, Mp3Meta &out)
{
    uint8_t hdr[10];
    f.seek(0);
    if (f.read(hdr, 10) != 10)
        return 0;
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3')
        return 0;

    // uint8_t ver   = hdr[3]; // ID3v2 major version
    uint8_t flags = hdr[5];
    uint32_t tagSize = syncsafe(hdr + 6) + 10; // includes the 10-byte header

    // Extended header?
    uint32_t extSize = 0;
    if (flags & 0x40)
    {
        uint8_t ext[4];
        if (f.read(ext, 4) == 4)
            extSize = syncsafe(ext); // ID3v2.4 uses syncsafe; v2.3 uses plain BE
        f.seek(10 + extSize);
    }

    uint32_t pos = 10 + extSize;
    const uint32_t end = tagSize;

    bool gotTitle = false;
    bool gotArtist = false;

    while (pos + 10 <= end && !(gotTitle && gotArtist))
    {
        uint8_t framehdr[10];
        f.seek(pos);
        if (f.read(framehdr, 10) != 10)
            break;

        // Padding / end of frames
        if (framehdr[0] == 0)
            break;

        uint32_t frameSize = readU32BE(framehdr + 4); // ID3v2.3 uses plain BE
        // For ID3v2.4 it should be syncsafe but many encoders don't comply — plain BE is safer
        if (frameSize == 0 || pos + 10 + frameSize > end)
            break;

        bool isTIT2 = (framehdr[0] == 'T' && framehdr[1] == 'I' && framehdr[2] == 'T' && framehdr[3] == '2');
        bool isTPE1 = (framehdr[0] == 'T' && framehdr[1] == 'P' && framehdr[2] == 'E' && framehdr[3] == '1');

        if (isTIT2 || isTPE1)
        {
            uint32_t readLen = (frameSize < 256) ? frameSize : 255;
            uint8_t buf[256];
            if (f.read(buf, readLen) == (int)readLen)
            {
                if (isTIT2 && !gotTitle)
                {
                    copyTag(out.title, buf, readLen, sizeof(out.title));
                    gotTitle = true;
                }
                if (isTPE1 && !gotArtist)
                {
                    copyTag(out.artist, buf, readLen, sizeof(out.artist));
                    gotArtist = true;
                }
            }
        }

        pos += 10 + frameSize;
    }

    return tagSize;
}

// ---------------------------------------------------------------------------
// ID3v1 tag (last 128 bytes of file)
// ---------------------------------------------------------------------------

static void parseId3v1(File &f, Mp3Meta &out)
{
    uint32_t fsize = f.size();
    if (fsize < 128)
        return;

    f.seek(fsize - 128);
    uint8_t tag[128];
    if (f.read(tag, 128) != 128)
        return;
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G')
        return;

    // title: bytes 3..32 (30 bytes) — ISO-8859-1 → UTF-8
    if (out.title[0] == '\0')
    {
        size_t di = 0;
        for (int i = 3; i < 33; ++i)
        {
            if (tag[i] == 0)
                break;
            if (!utf8Encode(out.title, di, sizeof(out.title), (uint32_t)(uint8_t)tag[i]))
                break;
        }
        out.title[di] = '\0';
    }

    // artist: bytes 33..62 (30 bytes) — ISO-8859-1 → UTF-8
    if (out.artist[0] == '\0')
    {
        size_t di = 0;
        for (int i = 33; i < 63; ++i)
        {
            if (tag[i] == 0)
                break;
            if (!utf8Encode(out.artist, di, sizeof(out.artist), (uint32_t)(uint8_t)tag[i]))
                break;
        }
        out.artist[di] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Duration estimation
// Reads the first valid MP3 frame header, checks for Xing/VBRI/Info header
// (which carries total frame count), or falls back to CBR estimation.
// ---------------------------------------------------------------------------

// MP3 bit-rate table [version][layer][index]  (kbps, 0 = free, -1 = bad)
static const int16_t kBitrateTable[2][3][16] = {
    // MPEG1
    {
        // Layer1
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1},
        // Layer2
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1},
        // Layer3
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1},
    },
    // MPEG2/2.5
    {
        // Layer1
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1},
        // Layer2/3
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1},
        // Layer2/3 (same table)
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1},
    }};

static const uint16_t kSampleRateTable[4][4] = {
    {44100, 48000, 32000, 0}, // MPEG1
    {22050, 24000, 16000, 0}, // MPEG2
    {11025, 12000, 8000, 0},  // MPEG2.5
    {0, 0, 0, 0}};

struct FrameInfo
{
    int bitrateKbps;
    int sampleRate;
    int samplesPerFrame;
    int frameBytes; // header + payload
    bool valid;
};

static FrameInfo parseFrameHeader(const uint8_t *h)
{
    FrameInfo fi = {};
    // sync word: 11 set bits
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
        return fi;

    int versionBits = (h[1] >> 3) & 0x03; // 00=2.5 01=res 10=2 11=1
    int layerBits = (h[1] >> 1) & 0x03;   // 01=3 10=2 11=1
    int bitrateIdx = (h[2] >> 4) & 0x0F;
    int srateIdx = (h[2] >> 2) & 0x03;
    int padding = (h[2] >> 1) & 0x01;

    if (versionBits == 0x01)
        return fi; // reserved
    if (layerBits == 0x00)
        return fi; // reserved

    int mpegVer = (versionBits == 0x03) ? 0 : (versionBits == 0x02 ? 1 : 2); // 0=MPEG1 1=MPEG2 2=MPEG2.5
    int srTable = (mpegVer == 0) ? 0 : (mpegVer == 1 ? 1 : 2);
    int layer = 3 - (layerBits - 1); // 1,2,3
    int brTable = (mpegVer == 0) ? 0 : 1;
    int brRow = layer - 1;
    if (brRow > 2)
        return fi;

    int bitrate = kBitrateTable[brTable][brRow][bitrateIdx];
    int srate = kSampleRateTable[srTable][srateIdx];

    if (bitrate <= 0 || srate == 0)
        return fi;

    int spf = (layer == 1) ? 384 : ((layer == 3 && mpegVer != 0) ? 576 : 1152);

    int slotSize = (layer == 1) ? 4 : 1;
    int frameSize = (layer == 1)
                        ? (12 * bitrate * 1000 / srate + padding) * 4
                        : (144 * bitrate * 1000 / srate + padding) * slotSize;

    fi.bitrateKbps = bitrate;
    fi.sampleRate = srate;
    fi.samplesPerFrame = spf;
    fi.frameBytes = frameSize;
    fi.valid = true;
    return fi;
}

static void estimateDuration(File &f, uint32_t audioStart, Mp3Meta &out)
{
    uint32_t audioBytes = f.size() - audioStart;
    // strip ID3v1 if present
    if (f.size() >= 128)
    {
        f.seek(f.size() - 128);
        uint8_t tag3[3];
        f.read(tag3, 3);
        if (tag3[0] == 'T' && tag3[1] == 'A' && tag3[2] == 'G')
            audioBytes -= 128;
    }

    // Read up to 2 KB to find first valid frame
    const uint32_t scanBuf = 2048;
    uint8_t buf[scanBuf];
    f.seek(audioStart);
    uint32_t got = f.read(buf, scanBuf);

    FrameInfo fi = {};
    uint32_t frameOffset = 0;
    for (uint32_t i = 0; i + 4 <= got; ++i)
    {
        fi = parseFrameHeader(buf + i);
        if (fi.valid)
        {
            frameOffset = i;
            break;
        }
    }
    if (!fi.valid)
        return; // can't determine duration

    // Check for Xing / Info VBR header (36 bytes after frame header start)
    // The Xing header is at:  frame_start + side_info_size + 4
    // For MPEG1 stereo=32, mono=17; MPEG2 stereo=17, mono=9
    // We look for "Xing" or "Info" at offset 36 from the frame start (MPEG1 stereo, most common)
    // and also at 13 (MPEG1 mono) and 21 (MPEG2 stereo).
    static const uint8_t kXingOffsets[] = {36, 13, 21, 9};
    bool vbrFound = false;
    for (uint8_t xi = 0; xi < 4 && !vbrFound; ++xi)
    {
        uint32_t xoff = frameOffset + 4 + kXingOffsets[xi];
        if (xoff + 8 > got)
            continue;
        if (!((buf[xoff] == 'X' || buf[xoff] == 'I') && buf[xoff + 1] == 'n' &&
              (buf[xoff + 2] == 'g' || buf[xoff + 2] == 'f') && buf[xoff + 3] == 'o'))
            continue;

        uint32_t flags = readU32BE(buf + xoff + 4);
        if (!(flags & 0x01))
            continue; // no frame count field
        if (xoff + 12 > got)
            continue;
        uint32_t totalFrames = readU32BE(buf + xoff + 8);
        if (totalFrames == 0)
            continue;

        out.durationSec = (uint32_t)((uint64_t)totalFrames * fi.samplesPerFrame / fi.sampleRate);
        vbrFound = true;
    }

    // Check VBRI header (Fraunhofer encoder) — always at frame_start + 36
    if (!vbrFound && frameOffset + 4 + 36 + 26 <= got)
    {
        uint32_t voff = frameOffset + 4 + 36;
        if (buf[voff] == 'V' && buf[voff + 1] == 'B' && buf[voff + 2] == 'R' && buf[voff + 3] == 'I')
        {
            uint32_t totalFrames = readU32BE(buf + voff + 14);
            if (totalFrames > 0)
            {
                out.durationSec = (uint32_t)((uint64_t)totalFrames * fi.samplesPerFrame / fi.sampleRate);
                vbrFound = true;
            }
        }
    }

    // CBR fallback: total_audio_bytes / frame_bytes * samples_per_frame / sample_rate
    if (!vbrFound && fi.frameBytes > 0)
    {
        uint32_t totalFrames = audioBytes / fi.frameBytes;
        out.durationSec = (uint32_t)((uint64_t)totalFrames * fi.samplesPerFrame / fi.sampleRate);
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void readMp3Meta(File &f, Mp3Meta &out)
{
    if (!f)
        return;

    uint32_t audioStart = parseId3v2(f, out);
    parseId3v1(f, out); // fills title/artist only if ID3v2 left them empty
    estimateDuration(f, audioStart, out);
}
