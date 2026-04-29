#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <AnimatedGIF.h>
#include "gif_data.h"
#include "config.h"
#include "spotify.h"

TFT_eSPI tft = TFT_eSPI();
AnimatedGIF gif;
SpotifyClient spotify;
volatile bool spotifyIsPlaying = false;
String spotifyTrackId;
String spotifyTrackName;
String spotifyArtistName;
volatile int spotifyProgressMs = 0;
volatile int spotifyDurationMs = 0;
volatile unsigned long spotifyFetchedAt = 0;
SemaphoreHandle_t spotifyMutex;
volatile uint8_t pendingAction = 0;  // 0=none, 1=toggle, 2=next, 3=prev

// Screen dimensions (landscape)
#define SCREEN_W 320
#define SCREEN_H 240

// Layout: full-width UI, disc centered with buttons flanking
#define DISC_SIZE 150
#define DISC_X ((SCREEN_W - DISC_SIZE) / 2)
#define DISC_Y 42
#define BARS_Y (DISC_Y + DISC_SIZE + 4)
#define BARS_H (SCREEN_H - BARS_Y - 2)
bool wasTouched = false;
bool showedPausedFrame = false;

// Procedural visualizer state
#define NUM_BARS 12
float barHeights[NUM_BARS] = {0};
float barTargets[NUM_BARS] = {0};
float barPhases[NUM_BARS];    // Phase offsets per bar
float barSpeeds[NUM_BARS];    // Speed per bar
unsigned long lastBarDraw = 0;
const unsigned long BAR_DRAW_INTERVAL = 80;  // Draw bars max ~12fps

// Scrolling text state
int scrollX = 0;
String displayText;
String lastDisplayText;
unsigned long lastScrollUpdate = 0;
const unsigned long SCROLL_INTERVAL = 30;  // Scroll speed

// Timestamp drawing
unsigned long lastTimestampDraw = 0;
const unsigned long TIMESTAMP_INTERVAL = 500;

// Background Spotify polling task (runs on core 0)
void spotifyTask(void *param) {
  const unsigned long POLL_INTERVAL = 3000;
  unsigned long lastPoll = 0;
  for (;;) {
    // Handle pending actions immediately
    uint8_t action = pendingAction;
    if (action) {
      pendingAction = 0;
      switch (action) {
        case 1: spotify.togglePlayback(spotifyIsPlaying); spotifyIsPlaying = !spotifyIsPlaying; break;
        case 2: spotify.skipNext(); break;
        case 3: spotify.skipPrev(); break;
      }
      lastPoll = 0;  // Force re-poll after action
      vTaskDelay(300 / portTICK_PERIOD_MS);
      continue;
    }

    // Poll Spotify on schedule
    if (millis() - lastPoll > POLL_INTERVAL) {
      lastPoll = millis();
      SpotifyState newState;
      if (spotify.fetchPlaybackState(newState)) {
        spotifyIsPlaying = newState.isPlaying;
        spotifyProgressMs = newState.progressMs;
        spotifyDurationMs = newState.durationMs;
        spotifyFetchedAt = millis();
        if (newState.trackId.length() > 0 && newState.trackId != spotifyTrackId) {
          spotifyTrackId = newState.trackId;
          spotifyTrackName = newState.trackName;
          spotifyArtistName = newState.artistName;
          scrollX = 0;  // Reset scroll on track change
          Serial.printf("Now playing: %s - %s\n", spotifyTrackName.c_str(), spotifyArtistName.c_str());
        }
      }
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void GIFDraw(GIFDRAW *pDraw) {
  uint16_t usTemp[320];
  uint16_t *usPalette = pDraw->pPalette;
  int y = pDraw->iY + pDraw->y + DISC_Y;
  int x = pDraw->iX + DISC_X;
  int iWidth = pDraw->iWidth;

  if (y >= SCREEN_H || x >= SCREEN_W) return;
  if (x + iWidth > SCREEN_W) iWidth = SCREEN_W - x;
  if (iWidth <= 0) return;

  uint8_t *s = pDraw->pPixels;

  if (pDraw->ucHasTransparency) {
    uint8_t ucTransparent = pDraw->ucTransparent;
    for (int i = 0; i < iWidth; i++) {
      usTemp[i] = (s[i] == ucTransparent) ? 0 : usPalette[s[i]];
    }
  } else {
    for (int i = 0; i < iWidth; i++) {
      usTemp[i] = usPalette[s[i]];
    }
  }

  tft.pushImage(x, y, iWidth, 1, usTemp);
}

// Update procedural bar targets using layered sine waves
void updateBars() {
  float t = millis() / 1000.0f;
  for (int i = 0; i < NUM_BARS; i++) {
    // Layer multiple sine waves for organic motion
    float v = 0;
    v += sinf(t * barSpeeds[i] + barPhases[i]) * 0.35f;
    v += sinf(t * barSpeeds[i] * 1.7f + barPhases[i] * 2.3f) * 0.25f;
    v += sinf(t * barSpeeds[i] * 0.5f + barPhases[i] * 0.7f) * 0.2f;
    // Add a pulse that hits all bars together (simulates a beat)
    v += sinf(t * 3.5f) * 0.2f;
    v = (v + 1.0f) / 2.0f;  // Normalize 0-1
    // Bass bars (left) tend higher, highs (right) tend quicker
    if (i < 4) v = v * 0.7f + 0.3f;
    barTargets[i] = v;
  }
}

// Smooth bars toward targets and draw across full width
void drawBars() {
  int barWidth = SCREEN_W / NUM_BARS;
  int maxBarHeight = BARS_H;
  int barY = BARS_Y;
  int barBaseX = 0;

  for (int i = 0; i < NUM_BARS; i++) {
    // Smooth: rise fast, fall slow
    if (barTargets[i] > barHeights[i]) {
      barHeights[i] += (barTargets[i] - barHeights[i]) * 0.6f;
    } else {
      barHeights[i] += (barTargets[i] - barHeights[i]) * 0.2f;
    }
    if (barHeights[i] < 0.01f) barHeights[i] = 0;

    int x = barBaseX + i * barWidth;
    int h = (int)(barHeights[i] * maxBarHeight);

    tft.fillRect(x, barY, barWidth - 1, maxBarHeight, TFT_BLACK);
    if (h > 0) {
      uint16_t color;
      if (i < 4) color = tft.color565(255, 50 + i * 40, 0);
      else if (i < 8) color = tft.color565(0, 200 + (i - 4) * 10, 50 + (i - 4) * 40);
      else color = tft.color565(50 + (i - 8) * 40, 100, 255);
      tft.fillRect(x, barY + maxBarHeight - h, barWidth - 1, h, color);
    }
  }
}

// Format milliseconds as M:SS
String formatTime(int ms) {
  int totalSec = ms / 1000;
  int m = totalSec / 60;
  int s = totalSec % 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d:%02d", m, s);
  return String(buf);
}

// Draw scrolling song name across full width
void drawScrollingTitle() {
  if (millis() - lastScrollUpdate < SCROLL_INTERVAL) return;
  lastScrollUpdate = millis();

  String newText = spotifyTrackName + "  -  " + spotifyArtistName;
  if (newText != lastDisplayText) {
    lastDisplayText = newText;
    displayText = newText;
    scrollX = 0;
  }

  tft.fillRect(0, 0, SCREEN_W, 18, TFT_BLACK);

  if (displayText.length() == 0) return;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int textWidth = tft.textWidth(displayText, 2);

  if (textWidth <= SCREEN_W) {
    tft.drawString(displayText, (SCREEN_W - textWidth) / 2, 2, 2);
  } else {
    String loopText = displayText + "   ***   ";
    int loopWidth = tft.textWidth(loopText, 2);
    tft.setViewport(0, 0, SCREEN_W, 18);
    tft.drawString(loopText, -scrollX, 2, 2);
    tft.drawString(loopText, -scrollX + loopWidth, 2, 2);
    tft.resetViewport();
    scrollX += 2;
    if (scrollX >= loopWidth) scrollX = 0;
  }
}

// Draw timestamps and progress bar across full width
void drawTimestamp() {
  if (millis() - lastTimestampDraw < TIMESTAMP_INTERVAL) return;
  lastTimestampDraw = millis();

  int progressMs = spotifyProgressMs;
  int durationMs = spotifyDurationMs;

  if (spotifyIsPlaying && spotifyFetchedAt > 0) {
    progressMs += (millis() - spotifyFetchedAt);
    if (progressMs > durationMs) progressMs = durationMs;
  }

  String current = formatTime(progressMs);
  String total = formatTime(durationMs);

  int y = 20;
  tft.fillRect(0, y, SCREEN_W, 18, TFT_BLACK);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(current, 4, y + 4, 1);
  tft.drawString(total, SCREEN_W - tft.textWidth(total, 1) - 4, y + 4, 1);

  // Progress bar
  int barX = 35;
  int barW = SCREEN_W - 70;
  int barH = 4;
  int barYpos = y + 6;
  float progress = (durationMs > 0) ? (float)progressMs / durationMs : 0;
  int filled = (int)(barW * progress);

  tft.drawRect(barX, barYpos, barW, barH, TFT_DARKGREY);
  if (filled > 0) {
    tft.fillRect(barX + 1, barYpos + 1, filled - 1, barH - 2, TFT_GREEN);
  }
}

// Skip buttons — flanking the disc
#define BTN_W 50
#define BTN_H 40
#define BTN_CY (DISC_Y + DISC_SIZE / 2)  // Vertically centered with disc
#define BTN_Y_POS (BTN_CY - BTN_H / 2)
#define BTN_PREV_X 10
#define BTN_NEXT_X (SCREEN_W - BTN_W - 10)

// Draw skip buttons on either side of disc
void drawSkipButtons() {
  // Prev button (left of disc)
  tft.drawRoundRect(BTN_PREV_X, BTN_Y_POS, BTN_W, BTN_H, 5, TFT_WHITE);
  int cx = BTN_PREV_X + BTN_W / 2;
  int cy = BTN_CY;
  tft.fillTriangle(cx + 4, cy - 10, cx + 4, cy + 10, cx - 8, cy, TFT_WHITE);
  tft.drawFastVLine(cx + 8, cy - 10, 21, TFT_WHITE);

  // Next button (right of disc)
  tft.drawRoundRect(BTN_NEXT_X, BTN_Y_POS, BTN_W, BTN_H, 5, TFT_WHITE);
  cx = BTN_NEXT_X + BTN_W / 2;
  cy = BTN_CY;
  tft.fillTriangle(cx - 4, cy - 10, cx - 4, cy + 10, cx + 8, cy, TFT_WHITE);
  tft.drawFastVLine(cx - 8, cy - 10, 21, TFT_WHITE);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  // Deassert touch and SD card CS before display init (shared SPI bus)
  pinMode(33, OUTPUT);
  digitalWrite(33, HIGH);
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);

  // Backlight
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(1);  // Landscape
  tft.fillScreen(TFT_BLACK);

  // Touch calibration for landscape — run once, then hardcode values
  uint16_t calData[5];
  tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
  tft.setTouch(calData);
  Serial.printf("Touch cal: %d,%d,%d,%d,%d\n", calData[0], calData[1], calData[2], calData[3], calData[4]);
  tft.fillScreen(TFT_BLACK);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 10, 110, 2);
  Serial.print("Connecting to WiFi");
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed! Check SSID/password. ESP32 only supports 2.4GHz.");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi FAILED", 10, 100, 4);
    tft.drawString("2.4GHz only!", 10, 140, 2);
    while (true) { delay(1000); }
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  // Initialize Spotify
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Connecting Spotify...", 10, 110, 2);
  if (spotify.begin()) {
    Serial.println("Spotify connected!");
  } else {
    Serial.println("Spotify auth failed - check credentials");
  }
  tft.fillScreen(TFT_BLACK);

  // Initialize bar phase/speed randomization
  randomSeed(analogRead(0));
  for (int i = 0; i < NUM_BARS; i++) {
    barPhases[i] = random(0, 628) / 100.0f;  // 0 to ~2*PI
    barSpeeds[i] = 1.5f + (random(0, 200) / 100.0f);  // 1.5 to 3.5
  }

  gif.begin(BIG_ENDIAN_PIXELS);
  Serial.printf("GIF data size: %d bytes\n", gif_data_len);

  // Draw skip buttons (static, drawn once)
  drawSkipButtons();

  // Start Spotify polling on core 0
  xTaskCreatePinnedToCore(spotifyTask, "spotify", 8192, NULL, 1, NULL, 0);
}

void loop() {
  // Update and draw visualizer bars (throttled)
  if (millis() - lastBarDraw > BAR_DRAW_INTERVAL) {
    lastBarDraw = millis();
    if (spotifyIsPlaying) {
      updateBars();
      drawBars();
    } else {
      bool anyActive = false;
      for (int i = 0; i < NUM_BARS; i++) {
        barTargets[i] = 0;
        barHeights[i] *= 0.9f;
        if (barHeights[i] > 0.01f) anyActive = true;
      }
      if (anyActive) drawBars();
    }
  }

  // Handle touch — toggle playback or skip
  uint16_t tx, ty;
  bool touching = tft.getTouch(&tx, &ty, 15);
  if (touching && !wasTouched) {
    if (tx >= BTN_PREV_X && tx <= BTN_PREV_X + BTN_W && ty >= BTN_Y_POS && ty <= BTN_Y_POS + BTN_H) {
      Serial.println("Touch: skip prev");
      pendingAction = 3;
    } else if (tx >= BTN_NEXT_X && tx <= BTN_NEXT_X + BTN_W && ty >= BTN_Y_POS && ty <= BTN_Y_POS + BTN_H) {
      Serial.println("Touch: skip next");
      pendingAction = 2;
    } else if (tx >= DISC_X && tx <= DISC_X + DISC_SIZE && ty >= DISC_Y && ty <= DISC_Y + DISC_SIZE) {
      Serial.println("Touch: toggle playback");
      pendingAction = 1;
    }
  }
  wasTouched = touching;

  // Draw UI elements
  drawScrollingTitle();
  drawTimestamp();

  // Play GIF frame (disc spins when playing, pauses when not)
  if (spotifyIsPlaying) {
    showedPausedFrame = false;
    if (gif.open((uint8_t *)gif_data, gif_data_len, GIFDraw)) {
      while (gif.playFrame(true, NULL)) {
        // Check touch during animation
        touching = tft.getTouch(&tx, &ty, 15);
        if (touching && !wasTouched) {
          if (tx >= BTN_PREV_X && tx <= BTN_PREV_X + BTN_W && ty >= BTN_Y_POS && ty <= BTN_Y_POS + BTN_H) {
            pendingAction = 3;
          } else if (tx >= BTN_NEXT_X && tx <= BTN_NEXT_X + BTN_W && ty >= BTN_Y_POS && ty <= BTN_Y_POS + BTN_H) {
            pendingAction = 2;
          } else if (tx >= DISC_X && tx <= DISC_X + DISC_SIZE && ty >= DISC_Y && ty <= DISC_Y + DISC_SIZE) {
            pendingAction = 1;
          }
        }
        wasTouched = touching;

        // Update bars during animation (throttled)
        if (millis() - lastBarDraw > BAR_DRAW_INTERVAL) {
          lastBarDraw = millis();
          updateBars();
          drawBars();
        }

        // Update UI during animation
        drawScrollingTitle();
        drawTimestamp();

        if (!spotifyIsPlaying) break;
        yield();
      }
      gif.close();
    }
  } else {
    // Show first frame of disc when paused/idle
    if (!showedPausedFrame) {
      if (gif.open((uint8_t *)gif_data, gif_data_len, GIFDraw)) {
        gif.playFrame(false, NULL);
        gif.close();
      }
      showedPausedFrame = true;
      drawSkipButtons();
    }
    delay(50);  // Don't busy-loop when paused
  }
}