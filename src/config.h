#pragma once

// All credentials are injected via build flags from .env file.
// Copy .env.example to .env and fill in your values.
// Do NOT hardcode secrets here.

#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Create a .env file — see .env.example"
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS not defined. Create a .env file — see .env.example"
#endif
#ifndef SPOTIFY_CLIENT_ID
#error "SPOTIFY_CLIENT_ID not defined. Create a .env file — see .env.example"
#endif
#ifndef SPOTIFY_CLIENT_SECRET
#error "SPOTIFY_CLIENT_SECRET not defined. Create a .env file — see .env.example"
#endif
#ifndef SPOTIFY_REFRESH_TOKEN
#error "SPOTIFY_REFRESH_TOKEN not defined. Create a .env file — see .env.example"
#endif
