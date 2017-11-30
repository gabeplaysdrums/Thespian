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

#define AUDIO_RECORD_SAMPLE_RATE 44100

#include <SdFat.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SerialFlash.h>
#include "record_sd_wav.h"

// Uncomment to test with a sine wave as input
#define TEST_INPUT_SINE 1

#if TEST_INPUT_SINE
// GUItool: begin automatically generated code
AudioSynthWaveformSine   sine1;          //xy=351,437
AudioRecordSdWavStereo   recWav1;          //xy=567,503
AudioAnalyzePeak         peak1;          //xy=569,372
AudioOutputI2S           i2s1;           //xy=571,439
AudioConnection          patchCord1(sine1, peak1);
AudioConnection          patchCord2(sine1, 0, i2s1, 0);
AudioConnection          patchCord3(sine1, 0, i2s1, 1);
AudioConnection          patchCord4(sine1, 0, recWav1, 0);
AudioConnection          patchCord5(sine1, 0, recWav1, 1);
AudioControlSGTL5000     sgtl5000_1;     //xy=319,765
// GUItool: end automatically generated code
#else
// GUItool: begin automatically generated code
AudioInputI2S            i2s2;           //xy=391,435
AudioRecordSdWavStereo   recWav1;          //xy=567,503
AudioAnalyzePeak         peak1;          //xy=569,372
AudioOutputI2S           i2s1;           //xy=571,439
AudioConnection          patchCord1(i2s2, 0, peak1, 0);
AudioConnection          patchCord2(i2s2, 0, i2s1, 0);
AudioConnection          patchCord3(i2s2, 0, recWav1, 0);
AudioConnection          patchCord4(i2s2, 1, i2s1, 1);
AudioConnection          patchCord5(i2s2, 1, recWav1, 1);
AudioControlSGTL5000     sgtl5000_1;     //xy=319,765
// GUItool: end automatically generated code
#endif

// For a stereo recording version, see this forum thread:
// https://forum.pjrc.com/threads/46150?p=158388&viewfull=1#post158388

// which input on the audio shield will be used?
const int myInput = AUDIO_INPUT_LINEIN;
//const int myInput = AUDIO_INPUT_MIC;


// Use these with the Teensy Audio Shield
//#define SDCARD_CS_PIN    10
//#define SDCARD_MOSI_PIN  7
//#define SDCARD_SCK_PIN   14

// Use these with the Teensy 3.5 & 3.6 SD card
// #define SDCARD_CS_PIN    BUILTIN_SDCARD
// #define SDCARD_MOSI_PIN  BUILTIN_SDCARD  // not actually used
// #define SDCARD_SCK_PIN   BUILTIN_SDCARD  // not actually used

// Use these for the SD+Wiz820 or other adaptors
//#define SDCARD_CS_PIN    4
//#define SDCARD_MOSI_PIN  11
//#define SDCARD_SCK_PIN   13

SdFatSdioEX sdEx;
File file;

// Remember which mode we're doing
int mode = 0;  // 0=stopped, 1=recording, 2=playing

elapsedMillis recordingMillis;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(30);

  // Enable the audio shield, select input, and enable output
  sgtl5000_1.enable();

#if TEST_INPUT_SINE
  sine1.amplitude(1.0);
  sine1.frequency(440.0);
  sine1.phase(0);
#else
  sgtl5000_1.inputSelect(myInput);
#endif
  sgtl5000_1.volume(0.5);

  // Initialize the SD card
#if 0
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here if no SD card, but print a message
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
#else
    if (!sdEx.begin()) {
        // stop here if no SD card, but print a message
        while (1) {
          Serial.println("Unable to access the SD card");
          delay(500);
        }
    }
    // make sdEx the current volume.
    sdEx.chvol();
#endif

  startRecording();
}


void loop() {

  // If we're playing or recording, carry on...
  if (mode == 1) {
    continueRecording();

    // if (recordingMillis > 600*1000)
    if (recordingMillis > 300*1000)
    // if (recordingMillis > 10*1000)
    {
        stopRecording();
    }
  }

  // when using a microphone, continuously adjust gain
  if (myInput == AUDIO_INPUT_MIC) adjustMicLevel();
}


void startRecording() {
  Serial.println("startRecording");
#if 0
  file = SD.open("RECORD.WAV", O_CREAT | O_WRITE);
#else
  // file.open("RECORD.WAV", O_CREAT | O_WRITE);
  Serial.print("Removing previously recorded file");
  file.remove(file.cwd(), "RECORD.WAV");
  file.createContiguous("RECORD.WAV", 100*1024*1024);
#endif
  recWav1.begin(&file);
  mode = 1;
  recordingMillis = 0;
}

void printRecordingStats() {
  Serial.print("Valid blocks: ");
  Serial.println(recWav1.validBlockCount());
  Serial.print("Dropped blocks: ");
  Serial.println(recWav1.droppedBlockCount());
  Serial.print("Partial blocks: ");
  Serial.println(recWav1.partialBlockCount());
  Serial.print("Max pending samples: ");
  Serial.println(recWav1.maxPendingSampleCount());
  Serial.println("Write speed (us): ");
  Serial.print("  average: ");
  Serial.print(recWav1.getWriteMicros().average);
  Serial.print("   stdev: ");
  Serial.print(recWav1.getWriteMicros().stdev);
  Serial.print("   min: ");
  Serial.print(recWav1.getWriteMicros().min);
  Serial.print("   max: ");
  Serial.println(recWav1.getWriteMicros().max);
  Serial.println("Update speed (us): ");
  Serial.print("  average: ");
  Serial.print(recWav1.getUpdateMicros().average);
  Serial.print("   stdev: ");
  Serial.print(recWav1.getUpdateMicros().stdev);
  Serial.print("   min: ");
  Serial.print(recWav1.getUpdateMicros().min);
  Serial.print("   max: ");
  Serial.println(recWav1.getUpdateMicros().max);
  Serial.println("Update interval (us): ");
  Serial.print("  average: ");
  Serial.print(recWav1.getUpdateIntervalMicros().average);
  Serial.print("   stdev: ");
  Serial.print(recWav1.getUpdateIntervalMicros().stdev);
  Serial.print("   min: ");
  Serial.print(recWav1.getUpdateIntervalMicros().min);
  Serial.print("   max: ");
  Serial.println(recWav1.getUpdateIntervalMicros().max);
  Serial.println("Write emptied interval (write count): ");
  Serial.print("  average: ");
  Serial.print(recWav1.getWriteEmptiedInterval().average);
  Serial.print("   stdev: ");
  Serial.print(recWav1.getWriteEmptiedInterval().stdev);
  Serial.print("   min: ");
  Serial.print(recWav1.getWriteEmptiedInterval().min);
  Serial.print("   max: ");
  Serial.print(recWav1.getWriteEmptiedInterval().max);
  Serial.print("   count: ");
  Serial.println(recWav1.getWriteEmptiedInterval().count);

  // recWav1.resetStatistics();
}

void continueRecording() {
  recWav1.process();

  static uint32_t prevElapsedPeriod = 0;
  const uint32_t elapsedPeriod = recordingMillis / 5000;

  if (elapsedPeriod != prevElapsedPeriod) {
    prevElapsedPeriod = elapsedPeriod;
    printRecordingStats();
    Serial.print("Recording for ");
    Serial.print(recordingMillis);
    Serial.println(" millis");
  }
}

void stopRecording() {
  Serial.println("stopRecording");
  recWav1.end();
#if TEST_INPUT_SINE
  sine1.amplitude(0);
#endif
  mode = 0;
  printRecordingStats();
}

void adjustMicLevel() {
  // TODO: read the peak1 object and adjust sgtl5000_1.micGain()
  // if anyone gets this working, please submit a github pull request :-)
}