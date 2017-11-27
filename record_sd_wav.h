#ifndef record_sd_wav_h_
#define record_sd_wav_h_

#include "Arduino.h"
#include "AudioStream.h"
#include "SD.h"
#include "memcpy_audio.h"

template<unsigned char ninput>
class AudioRecordSdWav : public AudioStream
{
public:
    AudioRecordSdWav(void) : AudioStream(ninput, inputQueueArray) { }

    void begin(const char* filename);
    void end();
    void process();

    // queue should be large enough to accommodate long SD writes
    static constexpr uint8_t InterleavedQueueLengthMillis = 100;
    static constexpr uint16_t InterleavedQueueBlockCount = static_cast<uint16_t>(
        InterleavedQueueLengthMillis *
        static_cast<size_t>(AUDIO_SAMPLE_RATE)/*sampls/sec*/ /
        1000/*millis/sec*/ /
        AUDIO_BLOCK_SAMPLES);
    static constexpr uint16_t InterleavedQueueSampleCount = (
        ninput *
        AUDIO_BLOCK_SAMPLES *
        InterleavedQueueBlockCount);

    uint16_t getDroppedBlockCount() const { return droppedBlocks; }

private:
    void update() override;
    uint16_t pendingInterleavedSamples() const;
    bool interleavedQueueHasCapacity() const;
    void drain(uint16_t samples);

    const uint16_t FileBlockSampleCount = 512 / sizeof(int16_t);
    audio_block_t* inputQueueArray[ninput];
    int16_t interleavedQueue[InterleavedQueueSampleCount];
    volatile uint16_t head = 0;
    volatile uint16_t tail = 0;
    File file;
    uint16_t droppedBlocks = 0;
    uint32_t dataChunkPos = 0;
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
        memcpy_tointerleaveLR(dst, src[0]->data, src[1]->data);
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
void AudioRecordSdWav<ninput>::begin(const char* filename) {
    file = SD.open(filename, FILE_WRITE);
    if (!file) return;

    constexpr char Header[] = "RIFF----WAVEfmt "; // (chunk size to be filled in later)
    file.write(Header, sizeof(Header) - 1); // do not write string null terminator

    // no extension data
    writeWordLittleEndian(file, 16, 4);
    // PCM - integer samples
    writeWordLittleEndian(file, 1, 2);
    // number of channels
    writeWordLittleEndian(file, ninput, 2);
    // samples per second (Hz)
    writeWordLittleEndian(file, static_cast<uint32_t>(AUDIO_SAMPLE_RATE), 4);
    // (Sample Rate * BitsPerSample * Channels) / 8
    writeWordLittleEndian(file, static_cast<uint32_t>(AUDIO_SAMPLE_RATE) * sizeof(int16_t) * ninput / 8, 4);
    // data block size (size of two integer samples, one for each channel, in bytes)
    writeWordLittleEndian(file, sizeof(int16_t) * ninput, 2);
    // number of bits per sample (use a multiple of 8)
    writeWordLittleEndian(file, 8 * sizeof(int16_t), 2);

    dataChunkPos = file.position();

    constexpr char DataChunk[] = "data----";
    file.write(DataChunk, sizeof(DataChunk) - 1); // do not write string null terminator
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::end() {
    if (!file) return;

    while (pendingInterleavedSamples() > FileBlockSampleCount) {
        drain(FileBlockSampleCount);
    }

    drain(pendingInterleavedSamples());

    // (We'll need the final file size to fix the chunk sizes above)
    const uint32_t fileLength = file.position();

    // Fix the data chunk header to contain the data size
    file.seek(dataChunkPos + 4);
    writeWordLittleEndian(file, fileLength - dataChunkPos + 8);

    // Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
    file.seek(0 + 4);
    writeWordLittleEndian(file, fileLength - 8, 4);

    file.flush();
    file.close();
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::process() {
    drain(FileBlockSampleCount);
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::drain(uint16_t sampleCount) {
    if (pendingInterleavedSamples() < sampleCount) return;

    uint32_t h = head, t = tail;

    if (h < t && InterleavedQueueSampleCount - t < sampleCount) {
        // Chunk is not contiguous.  Write two smaller chunks
        file.write(reinterpret_cast<const uint8_t*>(interleavedQueue + t), (InterleavedQueueSampleCount - t) * sizeof(int16_t));
        file.write(reinterpret_cast<const uint8_t*>(interleavedQueue), (sampleCount - (InterleavedQueueSampleCount - t)) * sizeof(int16_t));
    }
    else {
        // Chunk is contiguous, Write it to the file
        file.write(reinterpret_cast<const uint8_t*>(interleavedQueue + t), sampleCount * sizeof(int16_t));
    }

    // advance tail
    t += sampleCount;
    if (t >= InterleavedQueueSampleCount) {
        t -= InterleavedQueueSampleCount;
    }
    tail = t;
}

template<unsigned char ninput>
void AudioRecordSdWav<ninput>::update() {
    if (!file) return;

    audio_block_t* input[ninput];

    for (unsigned char channel=0; channel < ninput; channel++) {
        input[channel] = receiveReadOnly(channel);

        if (!input[channel]) {
            // failed to receive buffer.  clean up and exit
            for (unsigned char releaseChannel=0; releaseChannel < channel; releaseChannel++) {
                release(input[releaseChannel]);
            }
            return;
        }
    }

    if (interleavedQueueHasCapacity()) {
        uint32_t h = head;

        copySamplesInterleaved<ninput>(input, interleavedQueue + h);

        // advance head
        h += (ninput * AUDIO_BLOCK_SAMPLES);
        if (h >= InterleavedQueueSampleCount) {
            h -= InterleavedQueueSampleCount;
        }
        head = h;
    }
    else {
        // queue is not draining fast enough.  drop incoming samples
        droppedBlocks++;
    }

    for (unsigned char channel=0; channel < ninput; channel++) {
        release(input[channel]);
    }
}

template<unsigned char ninput>
uint16_t AudioRecordSdWav<ninput>::pendingInterleavedSamples() const {
    uint32_t h = head, t = tail;

    if (h < t) {
        h += InterleavedQueueSampleCount;
    }

    return h - t;
}

template<unsigned char ninput>
bool AudioRecordSdWav<ninput>::interleavedQueueHasCapacity() const {
    return (pendingInterleavedSamples() + ninput * AUDIO_BLOCK_SAMPLES) < InterleavedQueueSampleCount;
}

#endif // record_sd_wav_h_