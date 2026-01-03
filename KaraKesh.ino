#include <Adafruit_NeoPixel.h>
#include <driver/i2s.h>

// NeoPixel Configuration
#define LED_PIN 7
#define BUTTON_PIN 1
#define LED_COUNT 12
#define BRIGHTNESS 250

// INMP441 I2S Microphone Configuration
#define I2S_MIC_SERIAL_CLOCK 12  // BCLK
#define I2S_MIC_LEFT_RIGHT 11    // WS/LRCL
#define I2S_MIC_SERIAL_DATA 10   // DOUT
#define SAMPLE_RATE 44100
#define SAMPLE_COUNT 256
#define I2S_NUM I2S_NUM_0

// Audio processing settings
#define NOISE_FLOOR 0.005
#define PEAK_DECAY 0.98
#define BRIGHTNESS_SMOOTHING 0.95
#define MIN_MIC_BRIGHTNESS 10
#define MAX_MIC_BRIGHTNESS 255
#define AUDIO_GAIN 2.0

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

enum class LightMode : uint8_t {
  Mode1,
  Mode2,
  Mode3,
  Mic,
  Flash,
  Overbright
};
LightMode currentmode = LightMode::Mode1;

// Button state
int lastreading = HIGH;
bool buttonHandled = false;

// Audio buffers and state
int32_t raw_samples[SAMPLE_COUNT];
float current_level = 0;
float smoothed_level = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Audio-Reactive RGBW NeoPixel Controller");

  // Initialize NeoPixels
  strip.begin();
  strip.show();
  strip.setBrightness(BRIGHTNESS);

  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);

  // Initialize I2S microphone
  initMicrophone();

  Serial.println("Setup complete!");
}

void loop() {
  int buttonreading = digitalRead(BUTTON_PIN);

  // Check for button press (LOW when pressed with INPUT_PULLUP)
  if (buttonreading != lastreading) {
    if (buttonreading == LOW && !buttonHandled) {
      // Cycle to next mode
      switch (currentmode) {
        case LightMode::Mode1:
          currentmode = LightMode::Mode2;
          Serial.println("Switched to Mode 2");
          break;
        case LightMode::Mode2:
          currentmode = LightMode::Mode3;
          Serial.println("Switched to Mode 3");
          break;
        case LightMode::Mode3:
          currentmode = LightMode::Mic;
          Serial.println("Switched to Mic Mode");
          break;
        case LightMode::Mic:
          currentmode = LightMode::Flash;
          Serial.println("Switched to Flashlight");
          break;
        case LightMode::Flash:
          currentmode = LightMode::Overbright;
          Serial.println("Switched to Overbright mode");
          break;
        case LightMode::Overbright:
          currentmode = LightMode::Mode1;
          Serial.println("Switched to Mode 1");
          break;  
      }
      buttonHandled = true;
    }
    lastreading = buttonreading;
  } else if (buttonreading == HIGH) {
    buttonHandled = false;
  }

  // Run current mode
  switch (currentmode) {
    case LightMode::Mode1:
      rainbowFade2White(7, 12, 3);
      break;
    case LightMode::Mode2:
      whiteOverRainbow(75, 4);
      break;
    case LightMode::Mode3:
      colorWipeSequence();
      break;
    case LightMode::Mic:
      audioReactiveRainbow();
      break;
    case LightMode::Flash:
      strip.fill(strip.Color(0, 0, 0, strip.gamma8(255)));
      strip.show();
      checkButton();
      break;
    case LightMode::Overbright:
      strip.fill(strip.Color(255, 255, 255, strip.gamma8(255)));
      strip.show();
      checkButton();
      break;
  }
}

// ================= I2S MICROPHONE FUNCTIONS =================

void initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA
  };

  esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_NUM, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S set pin failed: %d\n", err);
    return;
  }

  Serial.println("I2S microphone initialized");
}

float processSample(int32_t raw) {
  // Convert 32-bit container to 24-bit sample
  raw >>= 8;

  // Convert to float (-1.0 to 1.0)
  float sample = raw / 8388608.0f;

  // DC offset removal
  static float dc_offset = 0;
  dc_offset = 0.9999f * dc_offset + 0.0001f * sample;
  return sample - dc_offset;
}

void captureAudio() {
  size_t bytes_read;
  i2s_read(I2S_NUM, raw_samples, sizeof(raw_samples), &bytes_read, 0);
}

float processAudioLevel() {
  captureAudio();

  float sum = 0;
  float max_val = 0;

  // Process each sample
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    float sample = processSample(raw_samples[i]);

    // Apply amplification
    sample *= AUDIO_GAIN;

    // Calculate RMS (Root Mean Square) for volume
    sum += sample * sample;
    max_val = fmax(max_val, fabs(sample));
  }

  // Calculate RMS value
  float rms = sqrt(sum / SAMPLE_COUNT);

  // Apply noise gate with lower threshold
  if (rms < NOISE_FLOOR) {
    rms = 0;
  }

  // Less smoothing for quicker response
  smoothed_level = (0.85 * smoothed_level) + (0.15 * rms);  // Faster response

  return smoothed_level;
}

float mapAudioToBrightness(float audio_level) {
  if (audio_level < NOISE_FLOOR) {
    return MIN_MIC_BRIGHTNESS;
  }

  // Normalize audio level (0 to 1)
  float normalized = min(audio_level * 10.0, 1.0);  // Multiply by 10 to amplify

  // Exponential curve: y = x^exponent
  // Lower exponent = more sensitivity to quiet sounds
  float exponent = 0.3;  // Lower = more sensitive (try 0.2 to 0.5)
  float curved = pow(normalized, exponent);

  // Map to brightness range
  return MIN_MIC_BRIGHTNESS + (curved * (MAX_MIC_BRIGHTNESS - MIN_MIC_BRIGHTNESS));
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ================= AUDIO-REACTIVE RAINBOW MODE =================

void audioReactiveRainbow() {
  for (long firstPixelHue = 0; firstPixelHue < 5 * 65536; firstPixelHue += 256) {
    float audio_level = processAudioLevel();

    // Always show some light, even with silence
    float mapped_brightness = mapAudioToBrightness(audio_level);
    int brightness = constrain((int)mapped_brightness, MIN_MIC_BRIGHTNESS, MAX_MIC_BRIGHTNESS);

    // Add a small random variation to make quiet sounds more interesting
    if (brightness < 120) {
      brightness += random(-5, 5);
    }

    strip.rainbow(firstPixelHue, 1, 255, brightness);
    strip.show();

    // Debug to help tune sensitivity
    static int debug_count = 0;
    if (debug_count++ > 50) {
      Serial.printf("Level: %.6f, Bright: %d, Norm: %.3f\n",
                    audio_level, brightness,
                    min(audio_level * 20.0, 1.0));
      debug_count = 0;
    }

    checkButton();
    if (currentmode != LightMode::Mic) return;

    delay(8);  // Slightly faster for more responsive feel
  }
}

// ================= EXISTING NEO-PIXEL MODES =================

void colorWipe(uint32_t color, int wait) {
  unsigned long startTime = millis();
  int i = 0;

  while (i < strip.numPixels()) {
    strip.setPixelColor(i, color);
    strip.show();

    // Check for button press during delay
    unsigned long currentTime = millis();
    while (millis() - currentTime < wait) {
      checkButton();
      delay(1);
    }

    i++;
  }
}

void colorWipeSequence() {
  colorWipe(strip.Color(255, 0, 0), 50);     // Red
  colorWipe(strip.Color(0, 255, 0), 50);     // Green
  colorWipe(strip.Color(0, 0, 255), 50);     // Blue
  colorWipe(strip.Color(0, 0, 0, 255), 50);  // White (RGBW)
}

void whiteOverRainbow(int whiteSpeed, int whiteLength) {
  if (whiteLength >= strip.numPixels()) whiteLength = strip.numPixels() - 1;

  int head = whiteLength - 1;
  int tail = 0;
  int loops = 3;
  int loopNum = 0;
  uint32_t lastTime = millis();
  uint32_t firstPixelHue = 0;

  while (loopNum < loops) {
    for (int i = 0; i < strip.numPixels(); i++) {
      if (((i >= tail) && (i <= head)) || ((tail > head) && ((i >= tail) || (i <= head)))) {
        strip.setPixelColor(i, strip.Color(0, 0, 0, 255));  // White for RGBW
      } else {
        int pixelHue = firstPixelHue + (i * 65536L / strip.numPixels());
        strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
      }
    }

    strip.show();
    firstPixelHue += 40;

    if ((millis() - lastTime) > whiteSpeed) {
      if (++head >= strip.numPixels()) {
        head = 0;
        loopNum++;
      }
      if (++tail >= strip.numPixels()) {
        tail = 0;
      }
      lastTime = millis();
    }

    checkButton();
    if (currentmode != LightMode::Mode2) return;
  }
}

void rainbowFade2White(int wait, int rainbowLoops, int whiteLoops) {
  int fadeVal = 0, fadeMax = 100;

  for (uint32_t firstPixelHue = 0; firstPixelHue < rainbowLoops * 65536; firstPixelHue += 256) {
    for (int i = 0; i < strip.numPixels(); i++) {
      uint32_t pixelHue = firstPixelHue + (i * 65536L / strip.numPixels());
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue, 255, 255 * fadeVal / fadeMax)));
    }

    strip.show();

    // Check for button during delay
    unsigned long startTime = millis();
    while (millis() - startTime < wait) {
      checkButton();
      delay(1);
    }

    if (firstPixelHue < 65536) {
      if (fadeVal < fadeMax) fadeVal++;
    } else if (firstPixelHue >= ((rainbowLoops - 1) * 65536)) {
      if (fadeVal > 0) fadeVal--;
    } else {
      fadeVal = fadeMax;
    }

    if (currentmode != LightMode::Mode1) return;
  }

  for (int k = 0; k < whiteLoops && currentmode == LightMode::Mode1; k++) {
    for (int j = 0; j < 256; j++) {
      strip.fill(strip.Color(0, 0, 0, strip.gamma8(j)));  // White fade for RGBW
      strip.show();
      checkButton();
      if (currentmode != LightMode::Mode1) return;
      delay(5);
    }

    // Check button during 1-second pause
    unsigned long startTime = millis();
    while (millis() - startTime < 1000 && currentmode == LightMode::Mode1) {
      checkButton();
      delay(1);
    }

    for (int j = 255; j >= 0; j--) {
      strip.fill(strip.Color(0, 0, 0, strip.gamma8(j)));  // White fade for RGBW
      strip.show();
      checkButton();
      if (currentmode != LightMode::Mode1) return;
      delay(5);
    }
  }
}

// ================= BUTTON HANDLING =================

void checkButton() {
  int buttonreading = digitalRead(BUTTON_PIN);

  if (buttonreading != lastreading) {
    if (buttonreading == LOW && !buttonHandled) {
      switch (currentmode) {
        case LightMode::Mode1:
          currentmode = LightMode::Mode2;
          Serial.println("Button: Switched to Mode 2");
          break;
        case LightMode::Mode2:
          currentmode = LightMode::Mode3;
          Serial.println("Button: Switched to Mode 3");
          break;
        case LightMode::Mode3:
          currentmode = LightMode::Mic;
          Serial.println("Button: Switched to Mic Mode");
          break;
        case LightMode::Mic:
          currentmode = LightMode::Mode1;
          Serial.println("Button: Switched to Mode 1");
          break;
      }
      buttonHandled = true;
    }
    lastreading = buttonreading;
  } else if (buttonreading == HIGH) {
    buttonHandled = false;
  }
}

// ================= ADDITIONAL AUDIO MODES (Optional) =================

// Alternative audio mode: Color changes with volume
void audioReactiveColorShift() {
  static uint32_t base_hue = 0;

  while (currentmode == LightMode::Mic) {
    // Process audio
    float audio_level = processAudioLevel();

    if (audio_level < NOISE_FLOOR) {
      strip.clear();
      strip.show();
    } else {
      // Map audio level to brightness
      float mapped_brightness = mapFloat(audio_level, NOISE_FLOOR, 0.5, MIN_MIC_BRIGHTNESS, MAX_MIC_BRIGHTNESS);
      int brightness = constrain((int)mapped_brightness, MIN_MIC_BRIGHTNESS, MAX_MIC_BRIGHTNESS);

      // Shift hue based on audio level
      base_hue += (int)(audio_level * 1000);
      if (base_hue > 65536) base_hue -= 65536;

      // Fill strip with single color based on hue
      uint32_t color = strip.gamma32(strip.ColorHSV(base_hue, 255, brightness));
      strip.fill(color);
      strip.show();
    }

    checkButton();
    delay(10);
  }
}
