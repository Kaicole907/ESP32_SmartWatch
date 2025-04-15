#include <Arduino.h>
#include <TFT_eSPI.h>
#include <utilities.h>
#include "global.h"
#include <WiFi.h>
#include "ble.h"
#include <SPIFFS.h>
#include <FS.h>
#include <vector>

// Button pin definitions

TFT_eSPI tft = TFT_eSPI();

// App State Machine
enum AppState {
    STATE_SETTINGS_WIFI,
    STATE_SETTINGS_BLE,
    STATE_HOME,
    STATE_TRACKING,
    STATE_SETTINGS_OVERVIEW
};
AppState currentState = STATE_SETTINGS_WIFI;

unsigned long backPressTime = 0;
bool backHeld = false;
bool prevBtn4State = HIGH;
unsigned long lastBtn4ClickTime = 0;
bool btn4Pressed = false;
bool trackingJustExited = false;
unsigned long trackingExitTime = 0;
int btn4ClickCount = 0;

int wifiMenuIndex = 0;
std::vector<String> ssidList;

unsigned long lastTrackingDraw = 0;
String lastTime = "";

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Welcome");

    tft.init();
    tft.setRotation(3);
    tft.setTextSize(2);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    drawWelcomeScreen();

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
    }

    pinMode(BTN1, INPUT_PULLUP);
    pinMode(BTN2, INPUT_PULLUP);
    pinMode(BTN3, INPUT_PULLUP);
    pinMode(BTN4, INPUT_PULLUP);

    lastTrackingDraw = millis(); // Initialize tracking draw timer
}

void loop() {
    // Read all buttons
    bool currentBtn4State = digitalRead(BTN4);

    // Debugging: Print button state changes
    if (digitalRead(BTN1) == LOW) Serial.println("[DEBUG] BTN1 (UP) pressed");
    if (digitalRead(BTN2) == LOW) Serial.println("[DEBUG] BTN2 (DOWN) pressed");
    if (digitalRead(BTN3) == LOW) Serial.println("[DEBUG] BTN3 (SELECT) pressed");
    if (digitalRead(BTN4) == LOW && prevBtn4State == HIGH) Serial.println("[DEBUG] BTN4 (BACK) pressed");

    // Handle BTN4 press for state switching (even in TRACKING)
    if (currentBtn4State == LOW && prevBtn4State == HIGH) {
        backPressTime = millis();
        backHeld = true;
        btn4Pressed = true;

        if (millis() - lastBtn4ClickTime < 500) {
            btn4ClickCount++;
        } else {
            btn4ClickCount = 1;
        }
        lastBtn4ClickTime = millis();
    }

    if (currentBtn4State == LOW && backHeld && millis() - backPressTime > 1500) {
        currentState = STATE_SETTINGS_OVERVIEW;
        backHeld = false;
        btn4Pressed = false;
        btn4ClickCount = 0;
    }

    if (currentBtn4State == HIGH && prevBtn4State == LOW) {
        if (backHeld && millis() - backPressTime <= 1500) {
            if (btn4Pressed) {
                if (btn4ClickCount == 2 && currentState == STATE_HOME && !trackingJustExited) {
                    currentState = STATE_TRACKING;
                    tft.fillScreen(TFT_BLACK);
                    Serial.println("[BTN4] Double click - Switched to TRACKING state");
                    btn4ClickCount = 0;
                } else if (currentState == STATE_TRACKING) {
                    currentState = STATE_HOME;
                    lastTime = "";
                    trackingExitTime = millis();
                    trackingJustExited = true;
                    tft.fillScreen(TFT_BLACK);
                    Serial.println("[BTN4] Switched back to CLOCK state");
                }
                btn4Pressed = false;
            }
        }
        backHeld = false;
    }

    prevBtn4State = currentBtn4State;

    // Reset tracking exit flag after 1 second
    if (trackingJustExited && millis() - trackingExitTime > 1000) {
        trackingJustExited = false;
    }

    switch (currentState) {
        case STATE_SETTINGS_WIFI: {
            static bool scanned = false;
            static bool needsRedraw = true;

            if (!scanned) {
                ssidList.clear();
                int n = WiFi.scanNetworks();
                for (int i = 0; i < n; ++i) {
                    ssidList.push_back(WiFi.SSID(i));
                }
                wifiMenuIndex = 0;
                scanned = true;
                needsRedraw = true;
            }

            handleWiFiButtons(wifiMenuIndex, ssidList.size() - 1, needsRedraw);

            if (needsRedraw) {
                drawWiFiMenu(ssidList, wifiMenuIndex, true);
                needsRedraw = false;
            }

            if (digitalRead(BTN3) == LOW) {
                Serial.println("[DEBUG] BTN3 (SELECT) clicked in WiFi menu");
                String selectedSSID = ssidList[wifiMenuIndex];
                String password = getSavedPassword(selectedSSID);
                if (password != "") {
                    WiFi.begin(selectedSSID.c_str(), password.c_str());
                    delay(3000);

                    initializeRTC();
                    initBLEProximity("Freenove");

                    currentState = STATE_SETTINGS_BLE;
                    scanned = false;
                } else {
                    drawCenteredText("No saved password", tft.height() / 2, 2, TFT_RED, TFT_BLACK);
                    delay(2000);
                }
            }
            break;
        }

        case STATE_SETTINGS_BLE:
            tft.fillScreen(TFT_BLACK);
            drawCenteredText("BLE Select", 20, 2, TFT_WHITE, TFT_BLACK);
            delay(2000);
            currentState = STATE_HOME;
            break;

        case STATE_HOME: {
            String currentTime = getTime().substring(0, 5);
            String currentDate = getDate();

            if (currentTime != lastTime) {
                tft.fillScreen(TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.setTextSize(6);
                tft.drawString(currentTime, tft.width() / 2, tft.height() / 2 - 30);

                tft.setTextSize(3);
                tft.setTextColor(TFT_YELLOW, TFT_BLACK);
                tft.drawString(currentDate, tft.width() / 2, tft.height() / 2 + 30);

                lastTime = currentTime;
            }
            break;
        }

        case STATE_TRACKING: {
            // Exit tracking mode if BTN4 is pressed
            if (digitalRead(BTN4) == LOW) {
                currentState = STATE_HOME;
                lastTime = "";
                trackingExitTime = millis();
                trackingJustExited = true;
                tft.fillScreen(TFT_BLACK);
                Serial.println("[TRACKING] BTN4 pressed - Exiting to CLOCK");
                break;
            }

            if (millis() - lastTrackingDraw >= 500) {
                lastTrackingDraw = millis();
                scanForFriend();

                int rectH = 100, rectW = 200;
                int rectX = (tft.width() - rectW) / 2;
                int rectY = (tft.height() - rectH) / 2;
                uint16_t color = TFT_RED;
                float distance = -1;
                int rssi = -100;

                if (isFriendSeen()) {
                    rssi = getFriendRSSI();
                    float txPower = -59;
                    float n = 2.0;
                    distance = pow(10.0, (txPower - rssi) / (10.0 * n));
                    if (distance < 1.5) color = TFT_GREEN;
                    else if (distance < 4.0) color = TFT_YELLOW;
                    else color = TFT_ORANGE;
                }

                tft.fillRect(rectX - 5, rectY - 5, rectW + 10, rectH + 10, TFT_BLACK);
                tft.fillRect(rectX, rectY, rectW, rectH, color);
                tft.drawRect(rectX, rectY, rectW, rectH, TFT_WHITE);

                tft.setTextDatum(TL_DATUM);
                tft.setTextColor(TFT_BLACK);
                tft.setTextSize(2);

                if (distance >= 0) {
                    char distBuf[20];
                    sprintf(distBuf, "~%.1f ft", distance * 3.28084);
                    tft.drawString("Distance:", rectX + 10, rectY + 10);
                    tft.drawString(distBuf, rectX + 120, rectY + 10);

                    char rssiBuf[20];
                    sprintf(rssiBuf, "%d dBm", rssi);
                    tft.drawString("Signal:", rectX + 10, rectY + 50);
                    tft.drawString(rssiBuf, rectX + 120, rectY + 50);
                } else {
                    tft.drawString("No device found", rectX + 10, rectY + 30);
                }
            }
            break;
        }

        case STATE_SETTINGS_OVERVIEW:
            tft.fillScreen(TFT_BLACK);
            drawCenteredText("Settings:", 20, 2, TFT_WHITE, TFT_BLACK);
            drawCenteredText("- WiFi Setup", 60, 2, TFT_WHITE, TFT_BLACK);
            drawCenteredText("- BLE Setup", 90, 2, TFT_WHITE, TFT_BLACK);
            delay(3000);
            currentState = STATE_HOME;
            break;
    }

    delay(100);
}