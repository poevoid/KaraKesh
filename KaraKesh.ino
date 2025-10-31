#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>  // Required for 16 MHz Adafruit Trinket
#endif

#define LED_PIN A2
#define BUTTON_PIN 6
#define MIC_PIN A0
#define LED_COUNT 12
#define BRIGHTNESS 250

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

enum class LightMode : uint8_t {
  Mode1,
  Mode2,
  Mode3,
  Mic
};
LightMode currentmode = LightMode::Mode1;

int lastreading = HIGH;
bool buttonHandled = false;
int micthreshold = 7000;
int noisefloor = 4350;

void setup() {
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  strip.begin();
  strip.show();
  strip.setBrightness(BRIGHTNESS);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MIC_PIN, INPUT);
  //Serial.begin(115200);
}

void loop() {
  int buttonreading = digitalRead(BUTTON_PIN);

  // Check for button press (LOW when pressed with INPUT_PULLUP)
  if (buttonreading != lastreading) {
    if (buttonreading == LOW && !buttonHandled) {
      // Cycle to next mode
      switch (currentmode) {
        case LightMode::Mode1: currentmode = LightMode::Mode2; break;
        case LightMode::Mode2: currentmode = LightMode::Mode3; break;
        case LightMode::Mode3: currentmode = LightMode::Mic; break;
        case LightMode::Mic: currentmode = LightMode::Mode1; break;
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
      rainbow(0);
      break;
  }
}

// Modified colorWipe with button checking
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
      delay(1);  // Small delay to prevent overwhelming the processor
    }

    i++;
  }
}

void colorWipeSequence() {
  colorWipe(strip.Color(255, 0, 0), 50);  // Red
  colorWipe(strip.Color(0, 255, 0), 50);  // Green
  colorWipe(strip.Color(0, 0, 255), 50);  // Blue
}

// Modified whiteOverRainbow with button checking
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
        strip.setPixelColor(i, strip.Color(0, 0, 0, 255));
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
    if (currentmode != LightMode::Mode2) return;  // Exit if mode changed
  }
}
void rainbow(int wait) {
  // Hue of first pixel runs 5 complete loops through the color wheel.
  // Color wheel has a range of 65536 but it's OK if we roll over, so
  // just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
  // means we'll make 5*65536/256 = 1280 passes through this loop:
  for (long firstPixelHue = 0; firstPixelHue < 5 * 65536; firstPixelHue += 256) {
    // strip.rainbow() can take a single argument (first pixel hue) or
    // optionally a few extras: number of rainbow repetitions (default 1),
    // saturation and value (brightness) (both 0-255, similar to the
    // ColorHSV() function, default 255), and a true/false flag for whether
    // to apply gamma correction to provide 'truer' colors (default true).
    int micreading = analogRead(MIC_PIN);
    //Serial.print("Mic:");Serial.println(micreading);
    if (micreading < noisefloor) {
      strip.clear();
      strip.show();
    } else {
      int mappedreading = map(micreading, noisefloor, micthreshold, 10, 255);
      //Serial.print("mapped:");Serial.println(mappedreading);
      strip.rainbow(firstPixelHue, 1, 255, mappedreading);
      // Above line is equivalent to:
      // strip.rainbow(firstPixelHue, 1, 255, 255, true);
      strip.show();  // Update strip with new contents
    }

    checkButton();
    if (currentmode != LightMode::Mic) return;  // Exit if mode changed
    delay(wait);                                // Pause for a moment
  }
}
// Modified rainbowFade2White with button checking
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

    if (currentmode != LightMode::Mode1) return;  // Exit if mode changed
  }

  for (int k = 0; k < whiteLoops && currentmode == LightMode::Mode1; k++) {
    for (int j = 0; j < 256; j++) {
      strip.fill(strip.Color(0, 0, 0, strip.gamma8(j)));
      strip.show();
      checkButton();
      if (currentmode != LightMode::Mode1) return;
      delay(5);  // Shorter delay for more responsive button
    }

    // Check button during 1-second pause
    unsigned long startTime = millis();
    while (millis() - startTime < 1000 && currentmode == LightMode::Mode1) {
      checkButton();
      delay(1);
    }

    for (int j = 255; j >= 0; j--) {
      strip.fill(strip.Color(0, 0, 0, strip.gamma8(j)));
      strip.show();
      checkButton();
      if (currentmode != LightMode::Mode1) return;
      delay(5);  // Shorter delay for more responsive button
    }
  }
}

// Button checking function
void checkButton() {
  int buttonreading = digitalRead(BUTTON_PIN);

  if (buttonreading != lastreading) {
    if (buttonreading == LOW && !buttonHandled) {
      switch (currentmode) {
        case LightMode::Mode1: currentmode = LightMode::Mode2; break;
        case LightMode::Mode2: currentmode = LightMode::Mode3; break;
        case LightMode::Mode3: currentmode = LightMode::Mic; break;
        case LightMode::Mic: currentmode = LightMode::Mode1; break;
      }
      buttonHandled = true;
    }
    lastreading = buttonreading;
  } else if (buttonreading == HIGH) {
    buttonHandled = false;
  }
}
