#ifndef DisplayService_h
#define DisplayService_h

#include <TM1637Display.h>
#include <RTClib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class DisplayService {
public:
    static constexpr uint8_t DEFAULT_CLK_PIN = 14;
    static constexpr uint8_t DEFAULT_DIO_PIN = 27;

    explicit DisplayService(uint8_t clkPin = DEFAULT_CLK_PIN, uint8_t dioPin = DEFAULT_DIO_PIN);
    void begin();

    // Atualiza a hora atual no visor (HH:MM)
    void showClock(const DateTime &now);

    // Exibe número de chaveiros resetados (ex: r003)
    void showResetCount(uint16_t count);

    // Exibe tempo decorrido (em segundos) no formato MM:SS
    void showElapsedTime(uint32_t totalSeconds);

    // Mostra "GO" piscando por tempo especificado (ms) de forma não-bloqueante
    void showGoBlinking(uint16_t durationMs = 2000, uint16_t blinkRateMs = 400);

    // Mostra "FIN" fixo por tempo especificado (ms) de forma não-bloqueante
    void showFin(uint16_t durationMs = 2000);

    // Mostra "Err" por tempo especificado (ms) de forma não-bloqueante
    void showError(uint16_t durationMs = 2000);

    // Mostra "E-L" (alerta de chegada sem largada / aviso) de forma não-bloqueante
    void showWarn(uint16_t durationMs = 2000);

    // Mostra "L-XX" (número da volta concluída no modo laps) de forma não-bloqueante
    void showLap(uint16_t lapNumber, uint16_t durationMs = 2000);

    // Atualização da máquina de estados (chamar no loop principal)
    void loop();

    // Limpa o display e cancela qualquer animação ativa
    void clear();

    // Interrompe animação em andamento
    void stopAnimation(bool clearDisplay = true);

    // Retorna se há animação ativa no momento
    bool isAnimating() const { return _animType != AnimationType::NONE; }

    uint8_t getClkPin() const { return _clkPin; }
    uint8_t getDioPin() const { return _dioPin; }

private:
    enum class AnimationType : uint8_t {
        NONE,
        GO_BLINKING,
        FIN,
        ERR,
        WARN,
        LAP
    };

    uint8_t _clkPin;
    uint8_t _dioPin;
    TM1637Display _display;
    SemaphoreHandle_t _mutex{nullptr};

    AnimationType _animType{AnimationType::NONE};
    unsigned long _animStartTime{0};
    uint32_t _animDurationMs{0};
    uint32_t _blinkRateMs{0};
    unsigned long _lastBlinkToggle{0};
    bool _blinkVisible{false};
};

#endif
