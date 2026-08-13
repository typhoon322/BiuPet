#pragma once

// Copy this file to secrets.h (gitignored) and fill in your WiFi credentials.
//
//   cp firmware/src/config/secrets.example.h firmware/src/config/secrets.h
//
// The firmware builds without secrets.h using the fallback values below,
// but the pet will not join your home network unless you create it.
#ifndef WIFI_SSID
#define WIFI_SSID "your-wifi-ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your-wifi-password"
#endif
