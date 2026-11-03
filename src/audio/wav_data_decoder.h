// wav_data_decoder.h — AudioTools' WAVDecoder with two fixes
//
// - It drops the audio in the first block: decodeHeader() returns the data
//   offset after parse() has cleared the header buffer, i.e. 0, so write()
//   discards the rest of that block — the first ~5.6 ms (the attack) of every
//   sample. write() below splits the first block at the data chunk itself.
// - It passes on everything after the header, so metadata after the audio
//   (LIST, id3, ... chunks) would play as noise. This one stops at the end of
//   the data chunk, and atDataEnd() tells the caller to stop reading.
#pragma once
#include <AudioTools.h>
#include <algorithm>
#include <cstring>

class WavDataDecoder : public WAVDecoder {
public:
    bool begin() override {
        dataWritten = 0;
        return WAVDecoder::begin();
    }

    size_t write(const uint8_t *data, size_t len) override {
        size_t start = isFirst ? dataStartIn(data, len) : 0;
        if (start == 0) return WAVDecoder::write(data, len);
        WAVDecoder::write(data, start); // just the header: parses it
        if (!isFirst && isValid && start < len) write_out(data + start, len - start);
        return len;
    }

    // True once the whole data chunk has been passed on: the rest of the file
    // is metadata and needn't be read at all.
    bool atDataEnd() {
        return !isFirst && dataWritten >= header.audioInfo().data_length;
    }

protected:
    size_t write_out(const uint8_t *data, size_t len) override {
        uint32_t limit = header.audioInfo().data_length;
        size_t n = dataWritten < limit ? std::min<size_t>(len, limit - dataWritten) : 0;
        dataWritten += n;
        if (n > 0) WAVDecoder::write_out(data, n);
        return len;
    }

private:
    uint32_t dataWritten = 0;

    // Where the audio starts in the first block, found the way the header
    // parser finds it; 0 when the header doesn't fit, which the original
    // code path then handles.
    static size_t dataStartIn(const uint8_t *data, size_t len) {
        size_t end = std::min<size_t>(len, MAX_WAV_HEADER_LEN);
        for (size_t i = 0; i + 8 <= end; ++i)
            if (memcmp(data + i, "data", 4) == 0) return i + 8;
        return 0;
    }
};
