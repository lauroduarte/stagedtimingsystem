#ifndef BuzzerService_h
#define BuzzerService_h

#include <Arduino.h>
#include <esp32-hal-ledc.h>

class BuzzerService {
public:
    BuzzerService(uint8_t pin);

    // Buzzer simples: duração em milissegundos
    void beep(unsigned int durationMs = 100);

    // Buzzer com frequência (passivo)
    void beepTone(unsigned int freqHz = 1000, unsigned int durationMs = 100);

private:
    void begin();
    uint8_t _pin;
    bool _initialized;
};

#endif
