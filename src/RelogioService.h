#ifndef RelogioService_h
#define RelogioService_h

#include <PsychicHttp.h>
#include <ESP32SvelteKit.h>
#include <RTClib.h>
#include <sys/time.h>
#include <functional>

#define RELOGIO_SETTINGS_ENDPOINT_PATH "/rest/relogio"

constexpr const char* DATETIME_FORMAT = "%Y-%m-%dT%H:%M:%S";

class Relogio {
public:
    std::string datetime;

    static void syncSystemTime(const DateTime &dt, uint16_t ms = 0) {
        timeval tv;
        tv.tv_sec = dt.unixtime();
        tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
        settimeofday(&tv, nullptr);
        Serial.printf("System POSIX time synchronized: %lu.%03u\n",
                      (unsigned long)tv.tv_sec,
                      (unsigned int)(ms % 1000));
    }

    static SemaphoreHandle_t getI2CMutex() {
        static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
        return mutex;
    }

    static bool &getHardwarePresentRef() {
        static bool hardwarePresent = false;
        return hardwarePresent;
    }

    static bool syncSystemTimeToDS3231Edge(bool waitEdge = false) {
        if (!isHardwarePresent()) {
            return false;
        }
        RTC_DS3231 &rtc = getRTC();
        SemaphoreHandle_t mtx = getI2CMutex();
        if (!mtx || xSemaphoreTake(mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
            return false;
        }

        DateTime dt = rtc.now();
        if (waitEdge) {
            uint8_t sec0 = dt.second();
            uint32_t startMs = millis();
            while (millis() - startMs < 1100) {
                dt = rtc.now();
                if (dt.second() != sec0) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        xSemaphoreGive(mtx);

        syncSystemTime(dt, 0);
        return true;
    }

    static RTC_DS3231& getRTC() {
        static RTC_DS3231 rtc;
        static bool initAttempted = false;
        if (!initAttempted) {
            initAttempted = true;
            SemaphoreHandle_t mtx = getI2CMutex();
            bool shouldSyncEdge = false;
            if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
            if (!rtc.begin()) {
                getHardwarePresentRef() = false;
                Serial.println("RTC DS3231 init failed - usando relógio interno POSIX");
            } else {
                getHardwarePresentRef() = true;
                if (rtc.lostPower()) {
                    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
                    Serial.println("RTC adjusted to compile time");
                }
                shouldSyncEdge = true;
            }
            if (mtx) xSemaphoreGive(mtx);

            if (shouldSyncEdge) {
                // Sincroniza na borda de transição do segundo no boot (após liberar mtx para evitar self-deadlock)
                syncSystemTimeToDS3231Edge(true);
            }
        }
        return rtc;
    }

    static bool isHardwarePresent() {
        getRTC();
        return getHardwarePresentRef();
    }

    static DateTime getNowMs(uint16_t &outMs) {
        getRTC();
        timeval tv{};
        gettimeofday(&tv, nullptr);
        outMs = static_cast<uint16_t>((tv.tv_usec / 1000) % 1000);
        return DateTime(static_cast<uint32_t>(tv.tv_sec));
    }

    static DateTime getNow() {
        // Garante que a sincronização inicial do RTC (se presente) já ocorreu no boot,
        // mas lê sempre o relógio POSIX interno em RAM para evitar colisões/travamentos
        // no barramento I2C (Error 263 / -1) durante chamadas concorrentes entre loop() e HTTP.
        uint16_t dummyMs = 0;
        return getNowMs(dummyMs);
    }

    static std::string formatDateTime(const DateTime &dt) {
        char buffer[25];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
                 dt.year(), dt.month(), dt.day(),
                 dt.hour(), dt.minute(), dt.second());
        return std::string(buffer);
    }

    static std::string formatDateTimeMs(const DateTime &dt, uint16_t ms) {
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03u",
                 dt.year(), dt.month(), dt.day(),
                 dt.hour(), dt.minute(), dt.second(),
                 static_cast<unsigned int>(ms % 1000));
        return std::string(buffer);
    }

    static std::string formatTimeHMSMs(const DateTime &dt, uint16_t ms) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03u",
                 dt.hour(), dt.minute(), dt.second(),
                 static_cast<unsigned int>(ms % 1000));
        return std::string(buffer);
    }

    static bool applyDateTimeString(const std::string &isoStr, int32_t offsetSeconds = 0, int32_t offsetMs = 0) {
        if (isoStr.empty()) {
            return false;
        }

        std::string limpa = isoStr;
        uint16_t parsedMs = 0;
        // Extrai frações de segundo (.mmm) e remove 'Z' se presentes
        size_t dotPos = limpa.find('.');
        if (dotPos != std::string::npos) {
            std::string frac = limpa.substr(dotPos + 1);
            int digits = 0;
            int val = 0;
            for (char c : frac) {
                if (c >= '0' && c <= '9' && digits < 3) {
                    val = val * 10 + (c - '0');
                    digits++;
                } else {
                    break;
                }
            }
            while (digits > 0 && digits < 3) {
                val *= 10;
                digits++;
            }
            parsedMs = static_cast<uint16_t>(val);
            limpa = limpa.substr(0, dotPos);
        }
        if (!limpa.empty() && (limpa.back() == 'Z' || limpa.back() == 'z')) {
            limpa.pop_back();
        }
        if (limpa.length() == 16) {
            limpa += ":00";
        }

        tm timeinfo = {};
        if (!strptime(limpa.c_str(), DATETIME_FORMAT, &timeinfo)) {
            return false;
        }
        DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        int64_t totalEpochMs = static_cast<int64_t>(dt.unixtime()) * 1000LL +
                               static_cast<int64_t>(parsedMs) +
                               static_cast<int64_t>(offsetSeconds) * 1000LL +
                               static_cast<int64_t>(offsetMs);
        uint32_t adjustedUnix = static_cast<uint32_t>(totalEpochMs / 1000LL);
        uint16_t adjustedMs = static_cast<uint16_t>((totalEpochMs % 1000LL + 1000LL) % 1000LL);
        dt = DateTime(adjustedUnix);

        // Ajusta o RTC se presente (protegido por mutex I2C)
        if (isHardwarePresent()) {
            RTC_DS3231 &rtc = getRTC();
            SemaphoreHandle_t mtx = getI2CMutex();
            if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
            rtc.adjust(dt);
            if (mtx) xSemaphoreGive(mtx);
        }

        // Sempre sincroniza o clock POSIX do sistema com precisão de milissegundos
        syncSystemTime(dt, adjustedMs);
        return true;
    }

    static void read(Relogio &settings, JsonObject &root) {
        Serial.println("READ RELOGIO");
        DateTime now = getNow();
        settings.datetime = formatDateTime(now);
        root["datetime"] = settings.datetime;
    }

    static StateUpdateResult update(JsonObject &root, Relogio &relogio) {
        if (!root["datetime"].is<const char*>()) return StateUpdateResult::ERROR;
        std::string newDatetime = root["datetime"].as<std::string>();
        int32_t offsetSec = root["offsetSeconds"].is<int>() ? root["offsetSeconds"].as<int>() : 0;
        int32_t offsetMs = root["offsetMs"].is<int>() ? root["offsetMs"].as<int>() : 0;

        if (!applyDateTimeString(newDatetime, offsetSec, offsetMs)) {
            return StateUpdateResult::ERROR;
        }

        relogio.datetime = formatDateTime(getNow());
        return StateUpdateResult::CHANGED;
    }
};

class RelogioService : public StatefulService<Relogio>
{
public:
    RelogioService(PsychicHttpServer *server,
                   ESP32SvelteKit *sveltekit, FS *fs);

    void begin();
    void loop();
    void setIsCronometrandoCallback(std::function<bool()> callback) {
        _isCronometrandoCallback = callback;
    }

private:
    PsychicHttpServer *_server;
    ESP32SvelteKit *_sveltekit;
    std::function<bool()> _isCronometrandoCallback{nullptr};

    esp_err_t handleGetRelogio(PsychicRequest *request);
    esp_err_t handlePostRelogio(PsychicRequest *request, JsonVariant &json);

    void registerConfig();
    void onConfigUpdated();
};

#endif
