#include "BuzzerService.h"


BuzzerService::BuzzerService(uint8_t pin) : _pin(pin), _initialized(false) {}

void BuzzerService::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    _initialized = true;
}

void BuzzerService::beep(unsigned int durationMs) {
    if (!_initialized) begin();
    digitalWrite(_pin, HIGH);
    delay(durationMs);
    digitalWrite(_pin, LOW);
}

void BuzzerService::beepTone(unsigned int freqHz, unsigned int durationMs) {
    if (!_initialized) begin();

    ledcWriteTone(0, freqHz);
    delay(durationMs);
    ledcWriteTone(0, 0);                 // Para o som
}
