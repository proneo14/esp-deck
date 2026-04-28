#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <AnimatedGIF.h>
#include "gif_data.h"

TFT_eSPI tft = TFT_eSPI();
AnimatedGIF gif;

void GIFDraw(GIFDRAW *pDraw) {
  uint16_t usTemp[320];
  uint16_t *usPalette = pDraw->pPalette;
  int y = pDraw->iY + pDraw->y;
  int x = pDraw->iX;
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

  gif.begin(BIG_ENDIAN_PIXELS);
  Serial.printf("GIF data size: %d bytes\n", gif_data_len);
}

void loop() {
  if (gif.open((uint8_t *)gif_data, gif_data_len, GIFDraw)) {
    Serial.printf("GIF opened: %dx%d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    while (gif.playFrame(true, NULL)) {
      yield();
    }
    gif.close();
  } else {
    Serial.println("GIF failed to open");
    delay(5000);
  }
}