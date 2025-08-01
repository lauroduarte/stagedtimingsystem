#ifndef RelogioService_h
#define RelogioService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <RTClib.h>


#define RELOGIO_SETTINGS_ENDPOINT_PATH "/rest/relogio"

constexpr const char* DATETIME_FORMAT = "%Y-%m-%dT%H:%M:%S";
    
class Relogio {
public:
    std::string datetime;

    static RTC_DS3231& getRTC() {
        static RTC_DS3231 rtc;
        static bool initialized = false;
        if (!initialized) {
            if (!rtc.begin()) {
                Serial.println("RTC init failed");
            } else if (rtc.lostPower()) {
                rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
                Serial.println("RTC adjusted to compile time");
            }
            initialized = true;
        }
        return rtc;
    }

    static void read(Relogio &settings, JsonObject &root) {
        Serial.println("READ RELOGIO");
        DateTime now = getRTC().now();
        char buffer[25];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
        settings.datetime = buffer;
        root["datetime"] = settings.datetime;
    }

    static StateUpdateResult update(JsonObject &root, Relogio &relogio) {
        if (!root.containsKey("datetime")) return StateUpdateResult::ERROR;
        relogio.datetime = root["datetime"].as<std::string>();

        tm timeinfo;
        if (!strptime(relogio.datetime.c_str(), DATETIME_FORMAT, &timeinfo)) {
            return StateUpdateResult::ERROR;
        }

        DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        getRTC().adjust(dt);
        return StateUpdateResult::CHANGED;
    }
};

class RelogioService : public StatefulService<Relogio>
{
public:
    RelogioService(PsychicHttpServer *server,
                      ESP32SvelteKit *sveltekit, FS *fs);

    void begin();

private:
    HttpEndpoint<Relogio> _httpEndpoint;

    void registerConfig();
    void onConfigUpdated();
};

#endif
