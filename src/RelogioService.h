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

    static void syncSystemTime(const DateTime &dt) {
        timeval tv;
        tv.tv_sec = dt.unixtime();
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        Serial.printf("System POSIX time synchronized: %lu\n", (unsigned long)tv.tv_sec);
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

        syncSystemTime(dt);
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

    static DateTime getNow() {
        // Garante que a sincronização inicial do RTC (se presente) já ocorreu no boot,
        // mas lê sempre o relógio POSIX interno em RAM para evitar colisões/travamentos
        // no barramento I2C (Error 263 / -1) durante chamadas concorrentes entre loop() e HTTP.
        getRTC();
        time_t t = time(nullptr);
        return DateTime(static_cast<uint32_t>(t));
    }

    static std::string formatDateTime(const DateTime &dt) {
        char buffer[25];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
                 dt.year(), dt.month(), dt.day(),
                 dt.hour(), dt.minute(), dt.second());
        return std::string(buffer);
    }

    static bool applyDateTimeString(const std::string &isoStr, int32_t offsetSeconds = 0) {
        if (isoStr.empty()) {
            return false;
        }

        std::string limpa = isoStr;
        // Remove 'Z' ou frações de segundo (.000Z) se presentes
        size_t dotPos = limpa.find('.');
        if (dotPos != std::string::npos) {
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
        if (offsetSeconds != 0) {
            uint32_t adjustedUnix = static_cast<uint32_t>(static_cast<int64_t>(dt.unixtime()) + offsetSeconds);
            dt = DateTime(adjustedUnix);
        }

        // Ajusta o RTC se presente (protegido por mutex I2C)
        if (isHardwarePresent()) {
            RTC_DS3231 &rtc = getRTC();
            SemaphoreHandle_t mtx = getI2CMutex();
            if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
            rtc.adjust(dt);
            if (mtx) xSemaphoreGive(mtx);
        }

        // Sempre sincroniza o clock POSIX do sistema
        syncSystemTime(dt);
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

        if (!applyDateTimeString(newDatetime, offsetSec)) {
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
