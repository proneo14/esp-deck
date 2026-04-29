#pragma once

// Secrets are auto-generated from .env at build time.
// Copy .env.example to .env and fill in your values.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found. Create a .env file — see .env.example"
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID not defined in .env"
#endif
#ifndef SPOTIFY_CLIENT_ID
#error "SPOTIFY_CLIENT_ID not defined in .env"
#endif
