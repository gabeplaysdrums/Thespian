#ifndef record_sd_wav_h_
#define record_sd_wav_h_

#include "Arduino.h"
#include "AudioStream.h"
#include "memcpy_audio.h"
#include "stats.h"

// Debugging

// Uncomment to debug AudioRecordSdWav
// #define DEBUG_AUDIO_RECORD_SD_WAV 1
// Uncomment to debug AudioRecordSdWav::update
// #define DEBUG_AUDIO_RECORD_SD_WAV_UPDATE 1
// Uncomment to debug AudioRecordSdWav::write
// #define DEBUG_AUDIO_RECORD_SD_WAV_WRITE 1
// Uncomment to debug dropped audio blocks
#define DEBUG_AUDIO_RECORD_SD_WAV_DROPPED_BLOCKS 1
// Uncomment to debug long file writes
#define DEBUG_AUDIO_RECORD_SD_WAV_WRITE_TOO_LONG 1


// Testing

// Uncomment to pre-allocate a contiguous file on the SD card
// #define TEST_AUDIO_RECORD_SD_WAV_PREALLOC_FILE 1

#ifndef AUDIO_RECORD_SAMPLE_RATE
#define AUDIO_RECORD_SAMPLE_RATE AUDIO_SAMPLE_RATE
#endif

template<unsigned char ninput>
class AudioRecordSdWav : public AudioStream
{
public:
    AudioRecordSdWav(void) : AudioStream(ninput, inputQueueArray) { }

    void begin(File* f);
    void end();
    void process();

    // queue should be large enough to accommodate long SD writes
    static constexpr uint16_t InterleavedQueueLengthMillis = 500;
    static constexpr uint16_t InterleavedQueueBlockCount = static_cast<uint16_t>(ceil(
        static_cast<float>(InterleavedQueueLengthMillis) *
        AUDIO_RECORD_SAMPLE_RATE/*samples/sec*/ /
        1000/*millis/sec*/ /
        AUDIO_BLOCK_SAMPLES /*samples/block*/));
    static constexpr uint32_t InterleavedQueueSampleCount = (
        ninput *
        AUDIO_BLOCK_SAMPLES *
        InterleavedQueueBlockCount);

    uint32_t validBlockCount() const { return validBlocks; }
    uint32_t droppedBlockCount() const { return droppedBlocks; }
    uint32_t partialBlockCount() const { return partialBlocks; }
    uint32_t maxPendingSampleCount() const { return maxPendingSamples; }
    const Stats<uint32_t>& getWriteMicros() { return writeMicros.get(); }
    const Stats<uint32_t>& getUpdateMicros() { return updateMicros.get(); }
    const Stats<uint32_t>& getUpdateIntervalMicros() { return updateIntervalMicros.get(); }
    const Stats<uint32_t>& getWriteEmptiedInterval() { return writeEmptiedInterval.get(); }
    void resetStatistics() {
        validBlocks = 0;
        droppedBlocks = 0;
        partialBlocks = 0;
        maxPendingSamples = 0;
        writeMicros.reset();
        updateMicros.reset();
        updateIntervalMicros.reset();
        writeEmptiedInterval.reset();
    }

private:
    void update() override;
    uint32_t pendingInterleavedSamples() const;
    bool interleavedQueueHasCapacity() const;
    void write(uint32_t sampleCount);

    const uint16_t FileBlockSampleCount = 512 / sizeof(int16_t);
    audio_block_t* inputQueueArray[ninput];
    int16_t interleavedQueue[InterleavedQueueSampleCount];
    volatile uint32_t head = 0;
    volatile uint32_t tail = 0;
    volatile bool empty = true;
    volatile bool enabled = false;
    File* file = nullptr;

    uint32_t validBlocks = 0;
    uint32_t droppedBlocks = 0;
    uint32_t partialBlocks = 0;
    uint32_t maxPendingSamples = 0;
    uint32_t writesSinceQueueEmptied = 0;
    uint32_t dataChunkPos = 0;

    StatsAccumulator<uint32_t> writeMicros;
    StatsAccumulator<uint32_t> updateMicros;
    StatsAccumulator<uint32_t> updateIntervalMicros;
    StatsAccumulator<uint32_t> writeEmptiedInterval;

    elapsedMicros updateInterval;
};

typedef AudioRecordSdWav<1> AudioRecordSdWavMono;
typedef AudioRecordSdWav<2> AudioRecordSdWavStereo;


namespace {
    template<unsigned char ninput>
    inline void copySamplesInterleaved(audio_block_t* src[ninput], int16_t* dst);

    template<>
    inline void copySamplesInterleaved<1>(audio_block_t* src[1], int16_t* dst) {
        memcpy(dst, src[0]->data, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    }

    template<>
    inline void copySamplesInterleaved<2>(audio_block_t* src[2], int16_t* dst) {
        // memcpy_tointerleaveLR assumes the output buffer has length AUDIO_BLOCK_SAMPLES,
        // so we need to copy each half separately
        memcpy_tointerleaveLR(dst, src[0]->data, src[1]->data);
        memcpy_tointerleaveLR(dst + AUDIO_BLOCK_SAMPLES, src[0]->data + AUDIO_BLOCK_SAMPLES/2, src[1]->data + AUDIO_BLOCK_SAMPLES/2);
    }

    static bool detectLittleEndian()
    {
        unsigned int x = 1;
        return reinterpret_cast<uint8_t*>(&x)[0] == 1;
    }

    static const bool IsLittleEndian = detectLittleEndian();

    template <typename Word>
    inline void writeWordLittleEndian(File& file, Word value, size_t size = sizeof(Word))
    {
        if (IsLittleEndian)
        {
            file.write(reinterpret_cast<const uint8_t*>(&value), size);
        }
        else
        {
            for (; size; --size, value >>= 8) {
                file.write(static_cast<uint8_t>(value & 0xFF));
            }
        }
    }
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::begin(File* f) {
#if DEBUG_AUDIO_RECORD_SD_WAV
    Serial.print("AudioRecordSdWav::begin()");
#endif

    file = f;
    if (!file || !*file) return;

    constexpr char Header[] = "RIFF----WAVEfmt "; // (chunk size to be filled in later)
    file->write(Header, sizeof(Header) - 1); // do not write string null terminator

    // no extension data
    writeWordLittleEndian(*file, 16, 4);
    // PCM - integer samples
    writeWordLittleEndian(*file, 1, 2);

    // number of channels
    constexpr uint16_t channelCount = ninput;
#if DEBUG_AUDIO_RECORD_SD_WAV
    Serial.print("Number of channels: ");
    Serial.println(channelCount);
#endif
    writeWordLittleEndian(*file, channelCount, 2);

    // samples per second (Hz)
    constexpr uint32_t sampleRate = static_cast<uint32_t>(AUDIO_RECORD_SAMPLE_RATE);
#if DEBUG_AUDIO_RECORD_SD_WAV
    Serial.print("Sample rate: ");
    Serial.println(sampleRate);
#endif
    writeWordLittleEndian(*file, sampleRate, 4);

    // (Sample Rate * BitsPerSample * Channels) / 8
    constexpr uint16_t bitsPerSample = 8 * sizeof(int16_t);
    constexpr uint32_t dataRate = sampleRate * bitsPerSample * channelCount / 8;
#if DEBUG_AUDIO_RECORD_SD_WAV
    Serial.print("Data rate: ");
    Serial.println(dataRate);
#endif
    writeWordLittleEndian(*file, dataRate, 4);

    // data block size (size of two integer samples, one for each channel, in bytes)
    constexpr uint16_t blockSize = sizeof(int16_t) * ninput;
#if DEBUG_AUDIO_RECORD_SD_WAV
    Serial.print("Block size: ");
    Serial.println(blockSize);
#endif
    writeWordLittleEndian(*file, blockSize, 2);

    // number of bits per sample (use a multiple of 8)
#if DEBUG_AUDIO_RECORD_SD_WAV
    Serial.print("Bits per sample: ");
    Serial.println(bitsPerSample);
#endif
    writeWordLittleEndian(*file, bitsPerSample, 2);

    dataChunkPos = file->position();

    constexpr char DataChunk[] = "data----";
    file->write(DataChunk, sizeof(DataChunk) - 1); // do not write string null terminator

    // incur penalty of flush now before samples start flowing
    file->flush();

    head = 0;
    tail = 0;
    empty = true;
    validBlocks = 0;
    partialBlocks = 0;
    droppedBlocks = 0;
    maxPendingSamples = 0;
    writesSinceQueueEmptied = 0;
    updateInterval = 0;
    resetStatistics();
    enabled = true;
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::end() {
    if (!enabled) return;
    enabled = false;

    while (pendingInterleavedSamples() > FileBlockSampleCount) {
        write(FileBlockSampleCount);
    }

    write(pendingInterleavedSamples());

    // (We'll need the final file size to fix the chunk sizes above)
    const uint32_t fileLength = file->position();

    // Fix the data chunk header to contain the data size
    file->seek(dataChunkPos + 4);
    writeWordLittleEndian(*file, fileLength - dataChunkPos + 8);

    // Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
    file->seek(0 + 4);
    writeWordLittleEndian(*file, fileLength - 8, 4);

    file->flush();

#if TEST_AUDIO_RECORD_SD_WAV_PREALLOC_FILE
    file->truncate(fileLength);
#endif

    file->close();
    file = nullptr;
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::process() {
    write(FileBlockSampleCount);
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::write(uint32_t sampleCount) {

    // __disable_irq();
    uint32_t pendingSamples = pendingInterleavedSamples();
#if DEBUG_AUDIO_RECORD_SD_WAV_WRITE
    uint32_t h = head;
#endif
    uint32_t t = tail;
    // __enable_irq();

    if (sampleCount == 0 || pendingSamples == 0 || pendingSamples < sampleCount) return;

#if DEBUG_AUDIO_RECORD_SD_WAV_WRITE
    Serial.print("AudioRecordSdWav::write(");
    Serial.print(sampleCount);
    Serial.println(")");

    Serial.print("head: ");
    Serial.print(h);
    Serial.print(" tail: ");
    Serial.print(t);
    Serial.print(" pending count: ");
    Serial.println(pendingSamples);
#endif

    elapsedMicros elapsed = 0;

    if (InterleavedQueueSampleCount - t < sampleCount) {
        // Chunk is not contiguous.  Write two smaller chunks
        const uint32_t sampleCount1 = InterleavedQueueSampleCount - t;
        const uint32_t sampleCount2 = sampleCount - sampleCount1;

#if DEBUG_AUDIO_RECORD_SD_WAV_WRITE
        Serial.print("Chunk is not contiguous. Doing 2 writes: ");
        Serial.print(sampleCount1);
        Serial.print(" ");
        Serial.println(sampleCount2);
#endif

        file->write(reinterpret_cast<const uint8_t*>(interleavedQueue + t), sampleCount1 * sizeof(int16_t));
        file->write(reinterpret_cast<const uint8_t*>(interleavedQueue), sampleCount2 * sizeof(int16_t));
    }
    else {
        // Chunk is contiguous, Write it to the file

#if DEBUG_AUDIO_RECORD_SD_WAV_WRITE
        Serial.print("Writing ");
        Serial.print(sampleCount);
        Serial.println(" samples");
#endif

        file->write(reinterpret_cast<const uint8_t*>(interleavedQueue + t), sampleCount * sizeof(int16_t));
    }

    // advance tail
    // __disable_irq();
    t += sampleCount;
    if (t >= InterleavedQueueSampleCount) {
        t -= InterleavedQueueSampleCount;
    }
    tail = t;
    empty = (head == tail);

#if DEBUG_AUDIO_RECORD_SD_WAV_WRITE_TOO_LONG
    if (empty) {
        writeEmptiedInterval.add(writesSinceQueueEmptied);
        writesSinceQueueEmptied = 0;
    }
    else {
        writesSinceQueueEmptied++;
    }
#endif

    // __enable_irq();

    writeMicros.add(elapsed);

#if DEBUG_AUDIO_RECORD_SD_WAV_WRITE_TOO_LONG
    if (elapsed > 50000) {
        Serial.print("Long write detected. elapsed (us): ");
        Serial.print(elapsed);
        Serial.print(" pendingSamples: ");
        Serial.println(pendingSamples);

        // if (enabled) {
        //     end();
        // }
    }
#endif
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::update() {
#if DEBUG_AUDIO_RECORD_SD_WAV_UPDATE
    if (enabled) {
        Serial.println("AudioRecordSdWav::update()");
    }
#endif

    // Update interval statistics
    updateIntervalMicros.add(updateInterval);
    updateInterval = 0;

    audio_block_t* input[ninput];
    uint8_t receivedCount = 0;

    for (unsigned char channel=0; channel < ninput; channel++) {
        input[channel] = receiveReadOnly(channel);

        if (!input[channel]) {
#if DEBUG_AUDIO_RECORD_SD_WAV_UPDATE
            if (enabled) {
                Serial.print("Failed to receive block from channel ");
                Serial.println(channel);
            }
#endif
            continue;
        }

        receivedCount++;

#if DEBUG_AUDIO_RECORD_SD_WAV_UPDATE
        if (enabled) {
            Serial.print("Received block from channel ");
            Serial.print(channel);
            Serial.print(". first: ");
            Serial.print(*(input[channel]->data));
            Serial.print(" last: ");
            Serial.println(*(input[channel]->data + AUDIO_BLOCK_SAMPLES - 1));
        }
#endif
    }

    if (!enabled) {
        for (unsigned char channel=0; channel < ninput; channel++) {
            if (input[channel]) {
                release(input[channel]);
            }
        }
        return;
    }

    uint32_t h = head;
    uint32_t pendingSamples = pendingInterleavedSamples();

#if DEBUG_AUDIO_RECORD_SD_WAV_UPDATE
    uint32_t t = tail;
    Serial.print("head: ");
    Serial.print(h);
    Serial.print(" tail: ");
    Serial.print(t);
    Serial.print(" pending count: ");
    Serial.print(pendingSamples);
    Serial.print(" (max ");
    Serial.print(InterleavedQueueSampleCount);
    Serial.println(")");
#endif

    if (pendingSamples > maxPendingSamples) {
        maxPendingSamples = pendingSamples;
    }

    if (receivedCount < ninput) {
        // not enough data received

        if (ninput > 1 && receivedCount != 0) {
            partialBlocks++;
        }
    }
    else {
        validBlocks++;

        if (interleavedQueueHasCapacity()) {
            elapsedMicros elapsed = 0;

            copySamplesInterleaved<ninput>(input, interleavedQueue + h);

#if DEBUG_AUDIO_RECORD_SD_WAV_UPDATE
            Serial.print("Copied samples.  first: ");
            Serial.print(*(interleavedQueue + h));
            Serial.print(" ");
            Serial.print(*(interleavedQueue + h + 1));
            Serial.print(" last: ");
            Serial.print(*(interleavedQueue + h + AUDIO_BLOCK_SAMPLES * ninput - 2));
            Serial.print(" ");
            Serial.println(*(interleavedQueue + h + AUDIO_BLOCK_SAMPLES * ninput - 1));
#endif

            // advance head
            h += (ninput * AUDIO_BLOCK_SAMPLES);
            if (h >= InterleavedQueueSampleCount) {
                h -= InterleavedQueueSampleCount;
            }
            head = h;
            empty = false;

            updateMicros.add(elapsed);
        }
        else {
            // queue is not draining fast enough.  drop incoming samples
            droppedBlocks++;

#if DEBUG_AUDIO_RECORD_SD_WAV_DROPPED_BLOCKS
            Serial.print("Audio block dropped! total: ");
            Serial.print(droppedBlocks);
            Serial.print(" pending samples: ");
            Serial.print(pendingSamples);
            Serial.print(" (max: ");
            Serial.print(InterleavedQueueSampleCount);
            Serial.print(")");
            Serial.print(" writes since emptied: ");
            Serial.println(writesSinceQueueEmptied);
#endif
        }
    }

    for (unsigned char channel=0; channel < ninput; channel++) {
        if (input[channel]) {
            release(input[channel]);
        }
    }
}

template<unsigned char ninput>
uint32_t AudioRecordSdWav<ninput>::pendingInterleavedSamples() const {
    if (head == tail) {
        return empty ? 0 : InterleavedQueueSampleCount;
    }

    uint32_t h = head, t = tail;

    if (h < t) {
        h += InterleavedQueueSampleCount;
    }

    return h - t;
}

template<unsigned char ninput>
bool AudioRecordSdWav<ninput>::interleavedQueueHasCapacity() const {
    return (pendingInterleavedSamples() + ninput * AUDIO_BLOCK_SAMPLES) <= InterleavedQueueSampleCount;
}

#endif // record_sd_wav_h_