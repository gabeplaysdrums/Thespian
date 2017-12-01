#include <Audio.h>
#include <FastLED.h>

// GUItool: begin automatically generated code
AudioInputI2S            i2s2;           //xy=319,391
AudioAnalyzePeak         peak2;          //xy=532,488
AudioAnalyzePeak         peak1;          //xy=533,447
AudioOutputI2S           i2s1;           //xy=573,391
AudioConnection          patchCord1(i2s2, 0, i2s1, 0);
AudioConnection          patchCord2(i2s2, 0, peak1, 0);
AudioConnection          patchCord3(i2s2, 1, i2s1, 1);
AudioConnection          patchCord4(i2s2, 1, peak2, 0);
AudioControlSGTL5000     sgtl5000_1;     //xy=319,765
// GUItool: end automatically generated code

FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define LED_DATA_PIN  2
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
#define NUM_LEDS      27
#define BRIGHTNESS    128
CRGB leds[NUM_LEDS];
int maxPeakIndexes[2] = { 0, 0 };
elapsedMillis maxPeakMillis[2] = { 0, 0 };

void setup() {
  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(30);

  // Enable the audio shield, select input, and enable output
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000_1.lineInLevel(15);
  sgtl5000_1.volume(0.5);

  FastLED.addLeds<LED_TYPE,LED_DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
  // put your main code here, to run repeatedly:

  if (peak1.available() && peak2.available()) {

    const auto setLeds = [](float level, int dir, int& maxLevelIndex, elapsedMillis& maxLevelMillis) {
      const int levelIndex = level > 0.999 ? (NUM_LEDS/2) + 1 : level * (NUM_LEDS/2);
      if (levelIndex > maxLevelIndex) {
        maxLevelIndex = levelIndex;
        maxLevelMillis = 0;
      }
      else if (maxLevelMillis > 5000 && maxLevelIndex > 0) {
        maxLevelIndex--;
        maxLevelMillis = 0;
      }
      
      for (int i=0; i <= NUM_LEDS / 2; i++) {
        CRGB color;
        if (static_cast<float>(i) / (NUM_LEDS/2) < level) {
          if (i == 0) {
            color = CRGB::Blue;
          }
          else if (i < 3) {
            color = CRGB::Green;
          }
          else if (i < NUM_LEDS/2 - 2) {
            color = CRGB::Yellow;
          }
          else {
            color = CRGB::Red;
          }
        }
        else {
          color = CRGB::Black;
        }
        leds[NUM_LEDS/2 + dir * i] = color;
      }

      if (maxLevelIndex > NUM_LEDS/2) {
        leds[NUM_LEDS/2 + dir * NUM_LEDS/2] = CRGB::Orange;
      }
      else if (maxLevelIndex > 0) {
        leds[NUM_LEDS/2 + dir * maxLevelIndex] = CRGB::White;
      }
    };

    setLeds(peak1.read(), -1, maxPeakIndexes[0], maxPeakMillis[0]);
    setLeds(peak2.read(), 1, maxPeakIndexes[1], maxPeakMillis[1]);
    FastLED.show();
  }
}
