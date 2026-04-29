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
SemaphoreHandle_t spotifyMutex;
bool pendingToggle = false;

int gifOffsetX = 0;
int gifOffsetY = 0;
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

// Background Spotify polling task (runs on core 0)
void spotifyTask(void *param) {
  const unsigned long POLL_INTERVAL = 3000;
  unsigned long lastPoll = 0;
  for (;;) {
    // Handle pending toggle immediately
    if (pendingToggle) {
      spotify.togglePlayback(spotifyIsPlaying);
      spotifyIsPlaying = !spotifyIsPlaying;
      pendingToggle = false;
      lastPoll = millis();  // Reset poll timer after toggle
    }

    // Poll Spotify on schedule
    if (millis() - lastPoll > POLL_INTERVAL) {
      lastPoll = millis();
      SpotifyState newState;
      if (spotify.fetchPlaybackState(newState)) {
        spotifyIsPlaying = newState.isPlaying;
        if (newState.trackId.length() > 0 && newState.trackId != spotifyTrackId) {
          spotifyTrackId = newState.trackId;
          spotifyTrackName = newState.trackName;
          spotifyArtistName = newState.artistName;
          Serial.printf("Now playing: %s - %s\n", spotifyTrackName.c_str(), spotifyArtistName.c_str());
        }
      }
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);  // Check for toggles every 50ms
  }
}

void GIFDraw(GIFDRAW *pDraw) {
  uint16_t usTemp[320];
  uint16_t *usPalette = pDraw->pPalette;
  int y = pDraw->iY + pDraw->y + gifOffsetY;
  int x = pDraw->iX + gifOffsetX;
  int iWidth = pDraw->iWidth;

  if (y >= 320 || x >= 240) return;
  if (x + iWidth > 240) iWidth = 240 - x;
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

// Smooth bars toward targets and draw
void drawBars() {
  int barWidth = 240 / NUM_BARS;
  int maxBarHeight = 60;
  int barY = 320 - maxBarHeight - 5;

  for (int i = 0; i < NUM_BARS; i++) {
    // Smooth: rise fast, fall slow
    if (barTargets[i] > barHeights[i]) {
      barHeights[i] += (barTargets[i] - barHeights[i]) * 0.6f;
    } else {
      barHeights[i] += (barTargets[i] - barHeights[i]) * 0.2f;
    }
    if (barHeights[i] < 0.01f) barHeights[i] = 0;

    int x = i * barWidth;
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
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Hardcoded touch calibration (from previous calibration run)
  uint16_t calData[5] = {650, 2852, 596, 3005, 3};
  tft.setTouch(calData);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 10, 150, 2);
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
    tft.drawString("WiFi FAILED", 10, 140, 4);
    tft.drawString("2.4GHz only!", 10, 180, 2);
    while (true) { delay(1000); }
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  // Initialize Spotify
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Connecting Spotify...", 10, 150, 2);
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

  // Handle touch — toggle Spotify playback
  uint16_t tx, ty;
  bool touching = tft.getTouch(&tx, &ty, 15);
  if (touching && !wasTouched) {
    Serial.println("Touch detected, toggling playback...");
    pendingToggle = true;  // Let background task handle it
  }
  wasTouched = touching;

  // Play GIF frame (disc spins when playing, pauses when not)
  if (spotifyIsPlaying) {
    showedPausedFrame = false;
    if (gif.open((uint8_t *)gif_data, gif_data_len, GIFDraw)) {
      gifOffsetX = (240 - gif.getCanvasWidth()) / 2;
      gifOffsetY = (320 - gif.getCanvasHeight()) / 2;
      while (gif.playFrame(true, NULL)) {
        // Check touch during animation
        touching = tft.getTouch(&tx, &ty, 15);
        if (touching && !wasTouched) {
          pendingToggle = true;
        }
        wasTouched = touching;

        // Update bars during animation (throttled)
        if (millis() - lastBarDraw > BAR_DRAW_INTERVAL) {
          lastBarDraw = millis();
          updateBars();
          drawBars();
        }

        if (!spotifyIsPlaying) break;
        yield();
      }
      gif.close();
    }
  } else {
    // Show first frame of disc when paused/idle
    if (!showedPausedFrame) {
      if (gif.open((uint8_t *)gif_data, gif_data_len, GIFDraw)) {
        gifOffsetX = (240 - gif.getCanvasWidth()) / 2;
        gifOffsetY = (320 - gif.getCanvasHeight()) / 2;
        gif.playFrame(false, NULL);
        gif.close();
      }
      showedPausedFrame = true;
    }
    delay(50);  // Don't busy-loop when paused
  }
}