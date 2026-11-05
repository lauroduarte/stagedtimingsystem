#include "BuzzerService.h"
#include <driver/gpio.h>

BuzzerService::BuzzerService(uint8_t pin, uint8_t channel)
    : _pin(pin), _channel(channel), _initialized(false) {
    _mutex = xSemaphoreCreateMutex();
}

void BuzzerService::begin() {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_initialized) {
        stopToneInternal();
        if (_mutex) xSemaphoreGive(_mutex);
        return;
    }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    // ESP32 Arduino Core 3.x: associa o pino diretamente ao periférico LEDC
    ledcAttach(_pin, DEFAULT_PWM_FREQ, DEFAULT_PWM_RESOLUTION);
    ledcWriteTone(_pin, 0);
#else
    // ESP32 Arduino Core 2.x: associa o canal de hardware ao pino
    ledcSetup(_channel, DEFAULT_PWM_FREQ, DEFAULT_PWM_RESOLUTION);
    ledcAttachPin(_pin, _channel);
    ledcWriteTone(_channel, 0);
#endif
    // Configura força máxima de corrente no pino GPIO (40mA - GPIO_DRIVE_CAP_3) para máximo volume em ambiente externo
    gpio_set_drive_capability(static_cast<gpio_num_t>(_pin), GPIO_DRIVE_CAP_3);

    _initialized = true;
    if (_mutex) xSemaphoreGive(_mutex);
}

void BuzzerService::playToneHardware(unsigned int freqHz) {
    if (!_initialized) {
        begin();
    }
    if (freqHz == 0) {
        stopToneInternal();
        return;
    }
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcWriteTone(_pin, freqHz);
#else
    ledcWriteTone(_channel, freqHz);
#endif
    gpio_set_drive_capability(static_cast<gpio_num_t>(_pin), GPIO_DRIVE_CAP_3);
}

void BuzzerService::stopToneInternal() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcWriteTone(_pin, 0);
#else
    ledcWriteTone(_channel, 0);
#endif
}

void BuzzerService::stop() {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    _isPlaying = false;
    _stepCount = 0;
    _currentStep = 0;
    stopToneInternal();
    if (_mutex) xSemaphoreGive(_mutex);
}

void BuzzerService::playPattern(const ToneStep *steps, size_t count) {
    if (steps == nullptr || count == 0) {
        stop();
        return;
    }

    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_initialized) {
        begin();
    }

    size_t toCopy = (count > MAX_BEEP_STEPS) ? MAX_BEEP_STEPS : count;
    for (size_t i = 0; i < toCopy; ++i) {
        _steps[i] = steps[i];
    }
    _stepCount = toCopy;
    _currentStep = 0;
    _stepStartMs = millis();
    _isPlaying = true;

    // Dispara imediatamente o primeiro tom
    if (_steps[0].freqHz > 0 && _steps[0].durationMs > 0) {
        playToneHardware(_steps[0].freqHz);
    } else {
        stopToneInternal();
    }

    if (_mutex) xSemaphoreGive(_mutex);
}

void BuzzerService::beep(unsigned int durationMs) {
    beepTone(DEFAULT_BEEP_FREQ, durationMs);
}

void BuzzerService::beepTone(unsigned int freqHz, unsigned int durationMs) {
    if (freqHz == 0 || durationMs == 0) {
        stop();
        return;
    }
    ToneStep step = { static_cast<uint16_t>(freqHz), static_cast<uint16_t>(durationMs) };
    playPattern(&step, 1);
}

void BuzzerService::beepError() {
    // Alerta sonoro forte bitonal na faixa de alta eficiência acústica para ambiente externo
    ToneStep steps[7] = {
        { static_cast<uint16_t>(FREQ_ERROR_HIGH), 220 },
        { 0,                                       50  },
        { static_cast<uint16_t>(FREQ_ERROR_LOW),  220 },
        { 0,                                       50  },
        { static_cast<uint16_t>(FREQ_ERROR_HIGH), 220 },
        { 0,                                       50  },
        { static_cast<uint16_t>(FREQ_ERROR_LOW),  320 }
    };
    playPattern(steps, 7);
}

void BuzzerService::beepWarning() {
    // Alerta triplo de alta pressão sonora (2400 Hz) para máxima audibilidade externa
    ToneStep steps[5] = {
        { static_cast<uint16_t>(FREQ_WARNING), 180 },
        { 0,                                    60  },
        { static_cast<uint16_t>(FREQ_WARNING), 180 },
        { 0,                                    60  },
        { static_cast<uint16_t>(FREQ_WARNING), 240 }
    };
    playPattern(steps, 5);
}

void BuzzerService::beepSuccess() {
    // Confirmação potente na frequência de ressonância (2730 Hz -> 3100 Hz) para ambiente externo
    ToneStep steps[3] = {
        { static_cast<uint16_t>(DEFAULT_BEEP_FREQ), 180 },
        { 0,                                         40  },
        { static_cast<uint16_t>(FREQ_SUCCESS_HIGH), 320 }
    };
    playPattern(steps, 3);
}

void BuzzerService::loop() {
    if (!_isPlaying) {
        return;
    }

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        if (_isPlaying) {
            unsigned long now = millis();
            if (now - _stepStartMs >= _steps[_currentStep].durationMs) {
                _currentStep++;
                if (_currentStep < _stepCount) {
                    _stepStartMs = now;
                    if (_steps[_currentStep].freqHz > 0 && _steps[_currentStep].durationMs > 0) {
                        playToneHardware(_steps[_currentStep].freqHz);
                    } else {
                        stopToneInternal();
                    }
                } else {
                    _isPlaying = false;
                    _stepCount = 0;
                    _currentStep = 0;
                    stopToneInternal();
                }
            }
        }
        xSemaphoreGive(_mutex);
    }
}
