#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration
extern const char *ssid;
extern const char *password;

// LINE Token Configuration
extern const char *lineToken;
extern const char *userId;

// Firebase Configuration
extern const char* API_KEY;
extern const char* DATABASE_URL;

// Pin Definitions
#define LED_GREEN 18
#define LED_RED 19
#define I2C_ADDR_KEYPAD 0x20
#define I2C_ADDR_LCD 0x27
#define pumpPin 2
#define moisturePin 34

// Timing Constants
extern unsigned long timerDelay;
extern int limitThreshold;

#endif
