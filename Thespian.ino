// Record sound as raw data to a SD card, and play it back.
//
// Requires the audio shield:
//   http://www.pjrc.com/store/teensy3_audio.html
//
// Three pushbuttons need to be connected:
//   Record Button: pin 0 to GND
//   Stop Button:   pin 1 to GND
//   Play Button:   pin 2 to GND
//
// This example code is in the public domain.

#include <Bounce.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

class MyAudioRecordQueue : public AudioStream
{
public:
    MyAudioRecordQueue(void) : AudioStream(1, inputQueueArray),
        userblock(NULL), head(0), tail(0), enabled(0) { }
    void begin(void) {
        clear();
        enabled = 1;
        overruns = 0;
    }
    int available(void);
    void clear(void);
    int16_t * readBuffer(void);
    void freeBuffer(void);
    void end(void) {
        enabled = 0;
    }
    virtual void update(void);

    size_t GetOverrunCount() const
    {
        return overruns;
    }
private:
    audio_block_t *inputQueueArray[1];
    audio_block_t * volatile queue[53];
    audio_block_t *userblock;
    volatile uint8_t head, tail, enabled;
    size_t overruns = 0;
};

// GUItool: begin automatically generated code
AudioInputI2S            i2s2;           //xy=105,63
AudioAnalyzePeak         peak1;          //xy=278,108
MyAudioRecordQueue       queue1;         //xy=281,63
MyAudioRecordQueue       queue2;
AudioPlaySdRaw           playRaw1;       //xy=302,157
AudioOutputI2S           i2s1;           //xy=470,120
AudioConnection          patchCord1(i2s2, 0, queue1, 0);
AudioConnection          patchCord2(i2s2, 0, peak1, 0);
AudioConnection          patchCord3(playRaw1, 0, i2s1, 0);
AudioConnection          patchCord4(playRaw1, 0, i2s1, 1);
AudioConnection          patchCord5(i2s2, 1, queue2, 0);
AudioControlSGTL5000     sgtl5000_1;     //xy=265,212
// GUItool: end automatically generated code

// For a stereo recording version, see this forum thread:
// https://forum.pjrc.com/threads/46150?p=158388&viewfull=1#post158388

// Bounce objects to easily and reliably read the buttons
Bounce buttonRecord = Bounce(0, 8);
Bounce buttonStop =   Bounce(1, 8);  // 8 = 8 ms debounce time
Bounce buttonPlay =   Bounce(2, 8);


// which input on the audio shield will be used?
const int myInput = AUDIO_INPUT_LINEIN;
//const int myInput = AUDIO_INPUT_MIC;


// Use these with the Teensy Audio Shield
//#define SDCARD_CS_PIN    10
//#define SDCARD_MOSI_PIN  7
//#define SDCARD_SCK_PIN   14

// Use these with the Teensy 3.5 & 3.6 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  BUILTIN_SDCARD  // not actually used
#define SDCARD_SCK_PIN   BUILTIN_SDCARD  // not actually used

// Use these for the SD+Wiz820 or other adaptors
//#define SDCARD_CS_PIN    4
//#define SDCARD_MOSI_PIN  11
//#define SDCARD_SCK_PIN   13


// Remember which mode we're doing
int mode = 0;  // 0=stopped, 1=recording, 2=playing

// The file where data is recorded
File frec;

elapsedMillis recordingMillis;

void setup() {
  // Configure the pushbutton pins
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);

  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(512);

  // Enable the audio shield, select input, and enable output
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(myInput);
  sgtl5000_1.volume(0.5);

  // Initialize the SD card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here if no SD card, but print a message
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }

  startRecording();
}


void loop() {
  // First, read the buttons
  buttonRecord.update();
  buttonStop.update();
  buttonPlay.update();

  // Respond to button presses
  if (buttonRecord.fallingEdge()) {
    Serial.println("Record Button Press");
    if (mode == 2) stopPlaying();
    if (mode == 0) startRecording();
  }
  if (buttonStop.fallingEdge()) {
    Serial.println("Stop Button Press");
    if (mode == 1) stopRecording();
    if (mode == 2) stopPlaying();
  }
  if (buttonPlay.fallingEdge()) {
    Serial.println("Play Button Press");
    if (mode == 1) stopRecording();
    if (mode == 0) startPlaying();
  }

  // If we're playing or recording, carry on...
  if (mode == 1) {
    continueRecording();

    if (recordingMillis > 30000)
    {
        stopRecording();
    }
  }
  if (mode == 2) {
    continuePlaying();
  }

  // when using a microphone, continuously adjust gain
  if (myInput == AUDIO_INPUT_MIC) adjustMicLevel();
}


void startRecording() {
  Serial.println("startRecording");
  if (SD.exists("RECORD.RAW")) {
    // The SD library writes new data to the end of the
    // file, so to start a new recording, the old file
    // must be deleted before new data is written.
    SD.remove("RECORD.RAW");
  }
  frec = SD.open("RECORD.RAW", FILE_WRITE);
  if (frec) {
    queue1.begin();
    queue2.begin();
    mode = 1;
    recordingMillis = 0;
  }
}

void continueRecording() {
  if (queue1.available() > 0 && queue2.available() > 0) {
    byte buffer[512];
    // Fetch 2 blocks from the audio library and copy
    // into a 512 byte buffer.  The Arduino SD library
    // is most efficient when full 512 byte sector size
    // writes are used.
    memcpy(buffer, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    memcpy(buffer+256, queue2.readBuffer(), 256);
    queue2.freeBuffer();
    // write all 512 bytes to the SD card
    elapsedMicros usec = 0;
    frec.write(buffer, 512);
    // Uncomment these lines to see how long SD writes
    // are taking.  A pair of audio blocks arrives every
    // 5802 microseconds, so hopefully most of the writes
    // take well under 5802 us.  Some will take more, as
    // the SD library also must write to the FAT tables
    // and the SD card controller manages media erase and
    // wear leveling.  The queue1 object can buffer
    // approximately 301700 us of audio, to allow time
    // for occasional high SD card latency, as long as
    // the average write time is under 5802 us.
    // Serial.print("SD write, us=");
    // Serial.println(usec);
  }
}

void stopRecording() {
  Serial.println("stopRecording");
  queue1.end();
  queue2.end();
  if (mode == 1) {
    while (queue1.available() > 0 && queue2.available() > 0) {
      frec.write((byte*)queue1.readBuffer(), 256);
      queue1.freeBuffer();
      frec.write((byte*)queue2.readBuffer(), 256);
      queue2.freeBuffer();
    }
    frec.close();

    Serial.print("overruns: ");
    Serial.println(queue1.GetOverrunCount());
  }
  mode = 0;
}


void startPlaying() {
  Serial.println("startPlaying");
  playRaw1.play("RECORD.RAW");
  mode = 2;
}

void continuePlaying() {
  if (!playRaw1.isPlaying()) {
    playRaw1.stop();
    mode = 0;
  }
}

void stopPlaying() {
  Serial.println("stopPlaying");
  if (mode == 2) playRaw1.stop();
  mode = 0;
}

void adjustMicLevel() {
  // TODO: read the peak1 object and adjust sgtl5000_1.micGain()
  // if anyone gets this working, please submit a github pull request :-)
}





#include "utility/dspinst.h"

int MyAudioRecordQueue::available(void)
{
    uint32_t h, t;

    h = head;
    t = tail;
    if (h >= t) return h - t;
    return 53 + h - t;
}

void MyAudioRecordQueue::clear(void)
{
    uint32_t t;

    if (userblock) {
        release(userblock);
        userblock = NULL;
    }
    t = tail;
    while (t != head) {
        if (++t >= 53) t = 0;
        release(queue[t]);
    }
    tail = t;
}

int16_t * MyAudioRecordQueue::readBuffer(void)
{
    uint32_t t;

    if (userblock) return NULL;
    t = tail;
    if (t == head) return NULL;
    if (++t >= 53) t = 0;
    // Serial.print("readBuffer.  available: ");
    // Serial.println(available());
    userblock = queue[t];
    tail = t;
    return userblock->data;
}

void MyAudioRecordQueue::freeBuffer(void)
{
    if (userblock == NULL) return;
    release(userblock);
    userblock = NULL;
    // Serial.println("freeBuffer");
}

void MyAudioRecordQueue::update(void)
{
    audio_block_t *block;
    uint32_t h;

    block = receiveReadOnly();
    if (!block) return;
    if (!enabled) {
        release(block);
        return;
    }
    h = head + 1;
    if (h >= 53)
    {
        h = 0;
        // Serial.println("Wrapping around ring buffer");
    }

    if (h == tail) {
        release(block);
        // Serial.print("Buffer dropped.  available: ");
        // Serial.println(available());
        overruns++;
    } else {
        queue[h] = block;
        head = h;
    }
}