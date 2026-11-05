#ifndef BuzzerService_h
#define BuzzerService_h

#include <Arduino.h>
#if defined(__has_include)
    #if __has_include(<esp_arduino_version.h>)
        #include <esp_arduino_version.h>
    #endif
#endif
#include <esp32-hal-ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class BuzzerService {
public:
    static constexpr uint8_t DEFAULT_BUZZER_PIN = 25;
    static constexpr uint8_t DEFAULT_PWM_CHANNEL = 0;
    static constexpr uint8_t DEFAULT_PWM_RESOLUTION = 10;
    // Frequências ajustadas para a banda de ressonância acústica máxima do buzzer (2400–3100 Hz)
    // garantindo máxima pressão sonora (dB) para uso em ambiente externo
    static constexpr unsigned int DEFAULT_PWM_FREQ = 2730;
    static constexpr unsigned int DEFAULT_BEEP_FREQ = 2730;
    static constexpr unsigned int FREQ_SUCCESS_HIGH = 3100;
    static constexpr unsigned int FREQ_WARNING = 2400;
    static constexpr unsigned int FREQ_ERROR_HIGH = 2500;
    static constexpr unsigned int FREQ_ERROR_LOW = 1950;

    explicit BuzzerService(uint8_t pin = DEFAULT_BUZZER_PIN, uint8_t channel = DEFAULT_PWM_CHANNEL);

    void begin();

    // Buzzer simples: duração em milissegundos (emite tom com frequência de ressonância máxima) de forma não-bloqueante
    void beep(unsigned int durationMs = 200);

    // Buzzer com frequência e duração em milissegundos de forma não-bloqueante
    void beepTone(unsigned int freqHz = DEFAULT_BEEP_FREQ, unsigned int durationMs = 200);

    // Emite alerta sonoro forte de erro para ambiente externo de forma não-bloqueante
    void beepError();

    // Emite alerta sonoro triplo de atenção de alta potência para ambiente externo de forma não-bloqueante
    void beepWarning();

    // Emite alerta sonoro potente de sucesso/confirmação para ambiente externo de forma não-bloqueante
    void beepSuccess();

    // Interrompe qualquer som ativo no buzzer
    void stop();

    // Processamento assíncrono dos tons (chamar no loop principal)
    void loop();

    // Retorna se há som/sequência em reprodução no momento
    bool isPlaying() const { return _isPlaying; }

private:
    struct ToneStep {
        uint16_t freqHz;
        uint16_t durationMs;
    };

    static constexpr size_t MAX_BEEP_STEPS = 8;

    uint8_t _pin;
    uint8_t _channel;
    bool _initialized;

    ToneStep _steps[MAX_BEEP_STEPS]{};
    size_t _stepCount{0};
    size_t _currentStep{0};
    unsigned long _stepStartMs{0};
    volatile bool _isPlaying{false};
    SemaphoreHandle_t _mutex{nullptr};

    void playToneHardware(unsigned int freqHz);
    void stopToneInternal();
    void playPattern(const ToneStep *steps, size_t count);
};

#endif
