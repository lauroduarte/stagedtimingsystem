#include "DisplayService.h"

namespace {
const uint8_t SEG_GO[] = {
    0x00,                                            // espaço
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F,           // G
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
    0x00                                             // espaço
};

const uint8_t SEG_FIN[] = {
    SEG_A | SEG_E | SEG_F | SEG_G,                   // F
    SEG_E | SEG_F,                                   // I (parecido com 1)
    SEG_C | SEG_E | SEG_G,                           // n
    0x00                                             // espaço
};

const uint8_t SEG_ERR[] = {
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,           // E
    SEG_E | SEG_G,                                   // r
    SEG_E | SEG_G,                                   // r
    0x00                                             // espaço
};

const uint8_t SEG_WARN[] = {
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,           // E
    SEG_G,                                           // -
    SEG_D | SEG_E | SEG_F,                           // L
    0x00                                             // espaço
};

class DisplayLock {
public:
    explicit DisplayLock(SemaphoreHandle_t mtx) : _mtx(mtx) {
        if (_mtx) xSemaphoreTake(_mtx, portMAX_DELAY);
    }
    ~DisplayLock() {
        if (_mtx) xSemaphoreGive(_mtx);
    }
private:
    SemaphoreHandle_t _mtx;
};
}

DisplayService::DisplayService(uint8_t clkPin, uint8_t dioPin)
    : _clkPin(clkPin), _dioPin(dioPin), _display(_clkPin, _dioPin) {
    _mutex = xSemaphoreCreateMutex();
}

void DisplayService::begin() {
    DisplayLock lock(_mutex);
    _animType = AnimationType::NONE;
    _display.setBrightness(0x0f); // brilho máximo
    _display.clear();
}

void DisplayService::showClock(const DateTime &now) {
    DisplayLock lock(_mutex);
    if (_animType != AnimationType::NONE) {
        return; // Não sobrepõe animação temporária ativa
    }
    _display.showNumberDecEx(now.hour() * 100 + now.minute(), 0b01000000, true); // dois pontos ligados e zero à esquerda
}

void DisplayService::showResetCount(uint16_t count) {
    DisplayLock lock(_mutex);
    _animType = AnimationType::NONE;
    if (count > 9999) count = 9999; // máximo para 4 dígitos
    _display.showNumberDec(count, true, 4, 0);
}

void DisplayService::showElapsedTime(uint32_t totalSeconds) {
    DisplayLock lock(_mutex);
    if (_animType != AnimationType::NONE) {
        return; // Não sobrepõe animação temporária ativa
    }
    uint8_t minutes = totalSeconds / 60;
    uint8_t seconds = totalSeconds % 60;
    uint16_t mmss = minutes * 100 + seconds;

    _display.showNumberDecEx(mmss, 0b01000000, true); // dois pontos entre minutos e segundos
}

void DisplayService::showGoBlinking(uint16_t durationMs, uint16_t blinkRateMs) {
    DisplayLock lock(_mutex);
    if (durationMs == 0) {
        _animType = AnimationType::NONE;
        _display.clear();
        return;
    }

    _animType = AnimationType::GO_BLINKING;
    _animDurationMs = durationMs;
    _blinkRateMs = (blinkRateMs > 0) ? blinkRateMs : 400;
    _animStartTime = millis();
    _lastBlinkToggle = _animStartTime;
    _blinkVisible = true;

    _display.setSegments(SEG_GO);
}

void DisplayService::showFin(uint16_t durationMs) {
    DisplayLock lock(_mutex);
    if (durationMs == 0) {
        _animType = AnimationType::NONE;
        _display.clear();
        return;
    }

    _animType = AnimationType::FIN;
    _animDurationMs = durationMs;
    _animStartTime = millis();

    _display.setSegments(SEG_FIN);
}

void DisplayService::showError(uint16_t durationMs) {
    DisplayLock lock(_mutex);
    if (durationMs == 0) {
        _animType = AnimationType::NONE;
        _display.clear();
        return;
    }

    _animType = AnimationType::ERR;
    _animDurationMs = durationMs;
    _animStartTime = millis();

    _display.setSegments(SEG_ERR);
}

void DisplayService::showWarn(uint16_t durationMs) {
    DisplayLock lock(_mutex);
    if (durationMs == 0) {
        _animType = AnimationType::NONE;
        _display.clear();
        return;
    }

    _animType = AnimationType::WARN;
    _animDurationMs = durationMs;
    _animStartTime = millis();

    _display.setSegments(SEG_WARN);
}

void DisplayService::showLap(uint16_t lapNumber, uint16_t durationMs) {
    DisplayLock lock(_mutex);
    if (durationMs == 0) {
        _animType = AnimationType::NONE;
        _display.clear();
        return;
    }

    if (lapNumber > 99) {
        lapNumber = 99;
    }

    _animType = AnimationType::LAP;
    _animDurationMs = durationMs;
    _animStartTime = millis();

    const uint8_t segLap[4] = {
        SEG_D | SEG_E | SEG_F,                       // L
        SEG_G,                                       // -
        _display.encodeDigit((lapNumber / 10) % 10), // dezena
        _display.encodeDigit(lapNumber % 10)         // unidade
    };
    _display.setSegments(segLap);
}

void DisplayService::loop() {
    DisplayLock lock(_mutex);
    if (_animType == AnimationType::NONE) {
        return;
    }

    unsigned long now = millis();

    // Verifica se a duração total da animação expirou
    if (now - _animStartTime >= _animDurationMs) {
        _animType = AnimationType::NONE;
        _display.clear();
        return;
    }

    if (_animType == AnimationType::GO_BLINKING) {
        if (now - _lastBlinkToggle >= _blinkRateMs) {
            _lastBlinkToggle = now;
            _blinkVisible = !_blinkVisible;
            if (_blinkVisible) {
                _display.setSegments(SEG_GO);
            } else {
                _display.clear();
            }
        }
    }
}

void DisplayService::clear() {
    DisplayLock lock(_mutex);
    _animType = AnimationType::NONE;
    _display.clear();
}

void DisplayService::stopAnimation(bool clearDisplay) {
    DisplayLock lock(_mutex);
    _animType = AnimationType::NONE;
    if (clearDisplay) {
        _display.clear();
    }
}
