#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

struct SpotifyState {
  bool isPlaying = false;
  int progressMs = 0;
  int durationMs = 0;
  String trackId;
  String trackName;
  String artistName;
  unsigned long fetchedAt = 0;  // millis() when state was fetched
};

class SpotifyClient {
public:
  bool begin() {
    return refreshAccessToken();
  }

  bool refreshAccessToken() {
    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification (ESP32 limitation)

    HTTPClient http;
    http.begin(client, "https://accounts.spotify.com/api/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Build Basic auth header
    String credentials = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
    String encoded = base64Encode(credentials);
    http.addHeader("Authorization", "Basic " + encoded);

    String body = "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN);
    int code = http.POST(body);

    if (code == 200) {
      JsonDocument doc;
      deserializeJson(doc, http.getStream());
      accessToken = doc["access_token"].as<String>();
      tokenExpiresAt = millis() + (doc["expires_in"].as<int>() - 60) * 1000UL;
      Serial.println("Spotify token refreshed");
      http.end();
      return true;
    }

    Serial.printf("Token refresh failed: %d\n", code);
    http.end();
    return false;
  }

  bool fetchPlaybackState(SpotifyState &state) {
    if (millis() > tokenExpiresAt) {
      if (!refreshAccessToken()) return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, "https://api.spotify.com/v1/me/player");
    http.addHeader("Authorization", "Bearer " + accessToken);

    int code = http.GET();
    if (code == 200) {
      JsonDocument doc;
      deserializeJson(doc, http.getStream());

      state.isPlaying = doc["is_playing"].as<bool>();
      state.progressMs = doc["progress_ms"].as<int>();
      state.durationMs = doc["item"]["duration_ms"].as<int>();
      state.trackId = doc["item"]["id"].as<String>();
      state.trackName = doc["item"]["name"].as<String>();
      state.artistName = doc["item"]["artists"][0]["name"].as<String>();
      state.fetchedAt = millis();

      http.end();
      return true;
    } else if (code == 204) {
      // No active device
      state.isPlaying = false;
      http.end();
      return true;
    }

    Serial.printf("Playback state failed: %d\n", code);
    http.end();
    return false;
  }

  bool togglePlayback(bool currentlyPlaying) {
    if (millis() > tokenExpiresAt) {
      if (!refreshAccessToken()) return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = currentlyPlaying
      ? "https://api.spotify.com/v1/me/player/pause"
      : "https://api.spotify.com/v1/me/player/play";
    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Length", "0");

    int code = http.PUT("");
    http.end();
    return (code == 200 || code == 204);
  }

  bool skipNext() {
    if (millis() > tokenExpiresAt) {
      if (!refreshAccessToken()) return false;
    }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.spotify.com/v1/me/player/next");
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Length", "0");
    int code = http.POST("");
    http.end();
    return (code == 200 || code == 204);
  }

  bool skipPrev() {
    if (millis() > tokenExpiresAt) {
      if (!refreshAccessToken()) return false;
    }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.spotify.com/v1/me/player/previous");
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Length", "0");
    int code = http.POST("");
    http.end();
    return (code == 200 || code == 204);
  }

private:
  String accessToken;
  unsigned long tokenExpiresAt = 0;

  String base64Encode(const String &input) {
    const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String result;
    int i = 0;
    uint8_t b3[3];
    uint8_t b4[4];
    int len = input.length();

    while (len--) {
      b3[i++] = input.charAt(input.length() - len - 1);
      if (i == 3) {
        b4[0] = (b3[0] & 0xfc) >> 2;
        b4[1] = ((b3[0] & 0x03) << 4) | ((b3[1] & 0xf0) >> 4);
        b4[2] = ((b3[1] & 0x0f) << 2) | ((b3[2] & 0xc0) >> 6);
        b4[3] = b3[2] & 0x3f;
        for (i = 0; i < 4; i++) result += chars[b4[i]];
        i = 0;
      }
    }

    if (i) {
      for (int j = i; j < 3; j++) b3[j] = 0;
      b4[0] = (b3[0] & 0xfc) >> 2;
      b4[1] = ((b3[0] & 0x03) << 4) | ((b3[1] & 0xf0) >> 4);
      b4[2] = ((b3[1] & 0x0f) << 2) | ((b3[2] & 0xc0) >> 6);
      for (int j = 0; j < i + 1; j++) result += chars[b4[j]];
      while (i++ < 3) result += '=';
    }

    return result;
  }
};
