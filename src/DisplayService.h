#ifndef DisplayService_h
#define DisplayService_h

#include <TM1637Display.h>
#include <RTClib.h>



class DisplayService {
public:
    DisplayService();
    void begin();

    // Atualiza a hora atual no visor (HH:MM)
    void showClock(const DateTime &now);

    // Exibe número de chaveiros resetados (ex: r003)
    void showResetCount(uint16_t count);

    // Exibe tempo decorrido (em segundos) no formato MM:SS
    void showElapsedTime(uint32_t totalSeconds);

    // Mostra "GO" piscando por tempo especificado (ms)
    void showGoBlinking(uint16_t durationMs = 2000, uint16_t blinkRateMs = 400);

    // Mostra "FIN" fixo por tempo especificado (ms)
    void showFin(uint16_t durationMs = 2000);    

    // Limpa o display
    void clear();

private:
    TM1637Display _display;
    const uint8_t clkPin = 1;
    const uint8_t dioPin = 2;
};

#endif
