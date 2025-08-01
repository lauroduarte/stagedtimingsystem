#include "DisplayService.h"

DisplayService::DisplayService()
    : _display(clkPin, dioPin) {}

void DisplayService::begin() {
    _display.setBrightness(0x0f); // brilho máximo
    _display.clear();
}

void DisplayService::showClock(const DateTime &now) {
    uint8_t data[] = {
        _display.encodeDigit(now.hour() / 10),
        _display.encodeDigit(now.hour() % 10),
        _display.encodeDigit(now.minute() / 10),
        _display.encodeDigit(now.minute() % 10)
    };
    _display.setSegments(data);
    _display.showNumberDecEx(now.hour() * 100 + now.minute(), 0b01000000); // dois pontos ligados
}

void DisplayService::showResetCount(uint16_t count) {
    if (count > 9999) count = 9999; // máximo para 4 dígitos
    _display.showNumberDec(count, true, 4, 0);
}

void DisplayService::showElapsedTime(uint32_t totalSeconds) {
    uint8_t minutes = totalSeconds / 60;
    uint8_t seconds = totalSeconds % 60;
    uint16_t mmss = minutes * 100 + seconds;

    _display.showNumberDecEx(mmss, 0b01000000, true); // dois pontos entre minutos e segundos
}

void DisplayService::showGoBlinking(uint16_t durationMs, uint16_t blinkRateMs) {
    const uint8_t SEG_GO[] = {
        0x00,                                            // espaço
        SEG_A | SEG_C | SEG_D | SEG_E | SEG_F,           // G
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
        0x00                                             // espaço
    };

    unsigned long start = millis();
    bool visible = true;

    while (millis() - start < durationMs) {
        if (visible) {
            _display.setSegments(SEG_GO);
        } else {
            _display.clear();
        }
        visible = !visible;
        delay(blinkRateMs);
    }

    _display.clear();
}


void DisplayService::showFin(uint16_t durationMs) {
    const uint8_t SEG_FIN[] = {
        SEG_A | SEG_E | SEG_F | SEG_G,                   // F
        SEG_E | SEG_F,                                   // I (parecido com 1)
        SEG_C | SEG_E | SEG_G,                           // n
        0x00                                             // espaço
    };

    _display.setSegments(SEG_FIN);
    delay(durationMs);
    _display.clear();
}

void DisplayService::clear() {
    _display.clear();
}

