# ESP Deck

A Spotify-connected ESP32 display that shows a spinning vinyl disc when music is playing, with procedural audio-reactive frequency bars. Touch the screen to pause/resume playback.

## Features

- **Spotify Integration** — Syncs with your Spotify account to show play/pause state in real-time
- **Spinning Disc** — Animated GIF of a vinyl disc, spins when playing, freezes when paused
- **Frequency Bars** — 12-band procedural visualizer at the bottom of the screen (bass=red, mids=green, highs=blue)
- **Touch Control** — Tap the touchscreen to toggle Spotify playback
- **Non-blocking** — Spotify API polling runs on core 0 via FreeRTOS, animation stays smooth on core 1

## Hardware

- ESP32 (DOIT DevKit V1 or similar)
- ILI9341 2.8" 240x320 TFT display with XPT2046 touch controller
- Shared SPI bus (HSPI)

### Pin Mapping

| Function | Pin |
|----------|-----|
| TFT MISO | 12 |
| TFT MOSI | 13 |
| TFT SCLK | 14 |
| TFT CS   | 15 |
| TFT DC   |  2 |
| Touch CS | 33 |
| Backlight| 21, 27 |

## Setup

### 1. Install PlatformIO

Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code.

### 2. Spotify Developer App

1. Go to [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard)
2. Create a new app
3. Set the redirect URI to `http://localhost:8888/callback`
4. Select **Web API**
5. Note your **Client ID** and **Client Secret**

### 3. Get Refresh Token

```bash
python get_spotify_token.py
```

Enter your Client ID and Secret when prompted. A browser window will open for Spotify authorization. The script will output a refresh token.

### 4. Configure Environment

Copy the example env file and fill in your credentials:

```bash
cp .env.example .env
```

Edit `.env` with your values:

```
WIFI_SSID=your_2.4ghz_wifi_name
WIFI_PASS=your_wifi_password
SPOTIFY_CLIENT_ID=your_client_id
SPOTIFY_CLIENT_SECRET=your_client_secret
SPOTIFY_REFRESH_TOKEN=your_refresh_token
```

> **Note:** ESP32 only supports 2.4GHz WiFi. 5GHz networks will not work.

### 5. Convert a GIF

Convert a GIF for the display using the included script:

```bash
python src/convert_gif.py your_disc.gif -W 200 -H 200 -s 1.0
```

Options:

| Flag | Description | Default |
|------|-------------|---------|
| `-W` | Width in pixels | 100 |
| `-H` | Height in pixels | 100 |
| `-s` | Speed multiplier (2.0 = 2x fast) | 1.0 |
| `-f` | Force specific FPS | original |
| `-c` | Max palette colors (≤256) | 256 |

### 6. Build & Upload

```bash
pio run --target upload
```

## Project Structure

```
├── .env.example          # Template for credentials
├── platformio.ini        # Build config & pin definitions
├── get_spotify_token.py  # One-time OAuth helper script
└── src/
    ├── main.cpp          # Main application (display, GIF, touch, bars)
    ├── spotify.h         # Spotify API client (auth, playback, toggle)
    ├── config.h          # Build flag validation
    ├── convert_gif.py    # GIF conversion tool
    ├── gif_data.h        # Generated GIF C array (from convert script)
    └── cd_flip.gif       # Source GIF
```
