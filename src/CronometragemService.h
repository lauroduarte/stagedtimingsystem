#ifndef CronometragemService_h
#define CronometragemService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <ChunkPrinter.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include "FixedString.h"

#define CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH "/rest/cronometragem"
#define CRONOMETRAGEM_JSONL_PATH "/config/cronometragem.jsonl"
#define CRONOMETRAGEM_LEGACY_JSON_PATH "/config/cronometragem.json"

struct CronoTipoField {
    uint8_t val = 1; // 0 = empty, 1 = especial, 2 = laps
    CronoTipoField() : val(1) {}
    CronoTipoField(const char* s) { set(s); }
    CronoTipoField(const std::string& s) { set(s.c_str()); }
    CronoTipoField(const String& s) { set(s.c_str()); }
    void set(const char* s) {
        if (!s || s[0] == '\0') {
            val = 0;
        } else if (std::strcmp(s, "laps") == 0 || std::strcmp(s, "LAPS") == 0) {
            val = 2;
        } else {
            val = 1;
        }
    }
    CronoTipoField& operator=(const char* s) { set(s); return *this; }
    CronoTipoField& operator=(const std::string& s) { set(s.c_str()); return *this; }
    CronoTipoField& operator=(const String& s) { set(s.c_str()); return *this; }
    const char* c_str() const {
        switch (val) {
            case 2: return "laps";
            case 1: return "especial";
            default: return "";
        }
    }
    bool empty() const { return val == 0; }
    bool operator==(const char* s) const { return std::strcmp(c_str(), s ? s : "") == 0; }
    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator==(const std::string& s) const { return std::strcmp(c_str(), s.c_str()) == 0; }
    bool operator!=(const std::string& s) const { return !(*this == s); }
    bool operator==(const CronoTipoField& o) const { return val == o.val; }
    bool operator!=(const CronoTipoField& o) const { return val != o.val; }
    operator const char*() const { return c_str(); }
    operator std::string() const { return std::string(c_str()); }
};

inline bool operator==(const char* s, const CronoTipoField& f) { return f == s; }
inline bool operator!=(const char* s, const CronoTipoField& f) { return f != s; }
inline bool operator==(const std::string& s, const CronoTipoField& f) { return f == s; }
inline bool operator!=(const std::string& s, const CronoTipoField& f) { return f != s; }

struct CronoModoVoltasField {
    uint8_t val = 1; // 0 = empty, 1 = unica, 2 = duas
    CronoModoVoltasField() : val(1) {}
    CronoModoVoltasField(const char* s) { set(s); }
    CronoModoVoltasField(const std::string& s) { set(s.c_str()); }
    CronoModoVoltasField(const String& s) { set(s.c_str()); }
    void set(const char* s) {
        if (!s || s[0] == '\0') {
            val = 0;
        } else if (std::strcmp(s, "duas") == 0 || std::strcmp(s, "DUAS") == 0) {
            val = 2;
        } else {
            val = 1;
        }
    }
    CronoModoVoltasField& operator=(const char* s) { set(s); return *this; }
    CronoModoVoltasField& operator=(const std::string& s) { set(s.c_str()); return *this; }
    CronoModoVoltasField& operator=(const String& s) { set(s.c_str()); return *this; }
    const char* c_str() const {
        switch (val) {
            case 2: return "duas";
            case 1: return "unica";
            default: return "";
        }
    }
    bool empty() const { return val == 0; }
    bool operator==(const char* s) const { return std::strcmp(c_str(), s ? s : "") == 0; }
    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator==(const std::string& s) const { return std::strcmp(c_str(), s.c_str()) == 0; }
    bool operator!=(const std::string& s) const { return !(*this == s); }
    bool operator==(const CronoModoVoltasField& o) const { return val == o.val; }
    bool operator!=(const CronoModoVoltasField& o) const { return val != o.val; }
    operator const char*() const { return c_str(); }
    operator std::string() const { return std::string(c_str()); }
};

inline bool operator==(const char* s, const CronoModoVoltasField& f) { return f == s; }
inline bool operator!=(const char* s, const CronoModoVoltasField& f) { return f != s; }
inline bool operator==(const std::string& s, const CronoModoVoltasField& f) { return f == s; }
inline bool operator!=(const std::string& s, const CronoModoVoltasField& f) { return f != s; }

struct CronoStatusField {
    uint8_t val = 1; // 0 = empty, 1 = OK, 2 = largada, 3 = sem_largada, 4 = invalido
    CronoStatusField() : val(1) {}
    CronoStatusField(const char* s) { set(s); }
    CronoStatusField(const std::string& s) { set(s.c_str()); }
    CronoStatusField(const String& s) { set(s.c_str()); }
    void set(const char* s) {
        if (!s || s[0] == '\0') {
            val = 0;
        } else if (std::strcmp(s, "OK") == 0 || std::strcmp(s, "ok") == 0) {
            val = 1;
        } else if (std::strcmp(s, "largada") == 0) {
            val = 2;
        } else if (std::strcmp(s, "sem_largada") == 0) {
            val = 3;
        } else if (std::strcmp(s, "invalido") == 0) {
            val = 4;
        } else {
            val = 1;
        }
    }
    CronoStatusField& operator=(const char* s) { set(s); return *this; }
    CronoStatusField& operator=(const std::string& s) { set(s.c_str()); return *this; }
    CronoStatusField& operator=(const String& s) { set(s.c_str()); return *this; }
    const char* c_str() const {
        switch (val) {
            case 1: return "OK";
            case 2: return "largada";
            case 3: return "sem_largada";
            case 4: return "invalido";
            default: return "";
        }
    }
    bool empty() const { return val == 0; }
    bool operator==(const char* s) const { return std::strcmp(c_str(), s ? s : "") == 0; }
    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator==(const std::string& s) const { return std::strcmp(c_str(), s.c_str()) == 0; }
    bool operator!=(const std::string& s) const { return !(*this == s); }
    bool operator==(const CronoStatusField& o) const { return val == o.val; }
    bool operator!=(const CronoStatusField& o) const { return val != o.val; }
    operator const char*() const { return c_str(); }
    operator std::string() const { return std::string(c_str()); }
};

inline bool operator==(const char* s, const CronoStatusField& f) { return f == s; }
inline bool operator!=(const char* s, const CronoStatusField& f) { return f != s; }
inline bool operator==(const std::string& s, const CronoStatusField& f) { return f == s; }
inline bool operator!=(const std::string& s, const CronoStatusField& f) { return f != s; }

struct cronometragem_t {
    int32_t duracaoSegundos = -1;
    int32_t duracaoMs = -1;
    int16_t numeral = 0;
    int16_t volta = 0;                      // 0 para estágios ou largada inicial (L0), 1..N para voltas
    FixedString<12> uid;
    FixedString<6> especial;                // "E1".."E10" no modo especial, ou "L0", "L1", "L2"... no modo laps
    FixedString<13> inicio;                 // "HH:MM:SS.mmm"
    FixedString<13> fim;                    // "HH:MM:SS.mmm"
    CronoTipoField tipo = "especial";       // "especial" | "laps"
    CronoModoVoltasField modoVoltas = "unica"; // "unica" | "duas" (quando tipo == "laps")
    CronoStatusField status = "OK";         // "largada" | "OK" | "sem_largada" | "invalido"
    bool incompleto = false;

    std::string duracao() const {
        if (duracaoMs >= 0) {
            return formatDurationMs(duracaoMs);
        }
        return formatDuration(duracaoSegundos);
    }

    std::string getDuracao() const {
        return duracao();
    }

    static std::string formatDurationMs(int32_t totalMs)
    {
        if (totalMs < 0)
        {
            return "";
        }
        int32_t totalSeconds = totalMs / 1000;
        int32_t ms = totalMs % 1000;
        int32_t hours = totalSeconds / 3600;
        int32_t minutes = (totalSeconds % 3600) / 60;
        int32_t seconds = totalSeconds % 60;
        char buf[20];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                      static_cast<int>(hours),
                      static_cast<int>(minutes),
                      static_cast<int>(seconds),
                      static_cast<int>(ms));
        return std::string(buf);
    }

    static std::string formatDuration(int32_t totalSeconds)
    {
        if (totalSeconds < 0)
        {
            return "";
        }
        return formatDurationMs(totalSeconds * 1000);
    }
};

static_assert(sizeof(cronometragem_t) <= 64, "cronometragem_t muito grande");

struct conflito_sync_t {
    FixedString<12> uid;
    int numeral = 0;
    FixedString<6> especial;
    FixedString<12> campo;       // "inicio" | "fim" | "inicio/fim"
    std::string valorLocal;
    std::string valorRemoto;
};

struct diagnostico_cronometragem_t {
    int totalRegistros = 0;
    int concluidos = 0;
    int pendentesChegada = 0;
    int semLargada = 0;
    int invalidos = 0;
};

struct resultado_merge_sync_t {
    int recebidos = 0;
    int inseridos = 0;
    int conciliados = 0;
    int duplicadosIgnorados = 0;
    int conflitosDetectados = 0;
    diagnostico_cronometragem_t diagnostico;
    std::vector<conflito_sync_t> conflitos;
};

class Cronometragem
{
public:
    std::deque<cronometragem_t> cronometragens;

    static bool parseTimeMs(const char *hms, int32_t &outTotalMs)
    {
        if (!hms || std::strlen(hms) < 8)
        {
            return false;
        }
        int h = -1, m = -1, s = -1;
        if (std::sscanf(hms, "%d:%d:%d", &h, &m, &s) != 3)
        {
            return false;
        }
        if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
        {
            return false;
        }
        int ms = 0;
        const char *dot = std::strchr(hms, '.');
        if (dot != nullptr)
        {
            const char *p = dot + 1;
            int digits = 0;
            while (*p >= '0' && *p <= '9' && digits < 3)
            {
                ms = ms * 10 + (*p - '0');
                p++;
                digits++;
            }
            while (digits > 0 && digits < 3)
            {
                ms *= 10;
                digits++;
            }
        }
        outTotalMs = (static_cast<int32_t>(h) * 3600 + static_cast<int32_t>(m) * 60 + static_cast<int32_t>(s)) * 1000 + ms;
        return true;
    }

    static bool parseTimeMs(const std::string &hms, int32_t &outTotalMs)
    {
        return parseTimeMs(hms.c_str(), outTotalMs);
    }

    static bool parseTimeSeconds(const char *hms, int32_t &outSeconds)
    {
        int32_t totalMs = 0;
        if (!parseTimeMs(hms, totalMs))
        {
            return false;
        }
        outSeconds = totalMs / 1000;
        return true;
    }

    static bool parseTimeSeconds(const std::string &hms, int32_t &outSeconds)
    {
        return parseTimeSeconds(hms.c_str(), outSeconds);
    }

    static std::string formatDurationMs(int32_t totalMs)
    {
        return cronometragem_t::formatDurationMs(totalMs);
    }

    static std::string formatDuration(int32_t totalSeconds)
    {
        return cronometragem_t::formatDuration(totalSeconds);
    }

    static bool calcularDuracaoMs(const char *inicio, const char *fim, int32_t &outMs, std::string &outFormatado)
    {
        int32_t msInicio = 0;
        int32_t msFim = 0;
        if (!parseTimeMs(inicio, msInicio) || !parseTimeMs(fim, msFim))
        {
            outMs = -1;
            outFormatado = "";
            return false;
        }
        if (msFim < msInicio)
        {
            msFim += 86400000; // Virada de meia-noite (24h em ms)
        }
        outMs = msFim - msInicio;
        outFormatado = formatDurationMs(outMs);
        return true;
    }

    static bool calcularDuracaoMs(const std::string &inicio, const std::string &fim, int32_t &outMs, std::string &outFormatado)
    {
        return calcularDuracaoMs(inicio.c_str(), fim.c_str(), outMs, outFormatado);
    }

    static bool calcularDuracao(const char *inicio, const char *fim, int32_t &outSegundos, std::string &outFormatado)
    {
        int32_t ms = -1;
        if (!calcularDuracaoMs(inicio, fim, ms, outFormatado))
        {
            outSegundos = -1;
            return false;
        }
        outSegundos = ms / 1000;
        return true;
    }

    static bool calcularDuracao(const std::string &inicio, const std::string &fim, int32_t &outSegundos, std::string &outFormatado)
    {
        return calcularDuracao(inicio.c_str(), fim.c_str(), outSegundos, outFormatado);
    }

    static void preencherDerivados(cronometragem_t &c)
    {
        if (c.tipo.empty())
        {
            c.tipo = (!c.especial.empty() && c.especial[0] == 'L') ? "laps" : "especial";
        }
        if (c.modoVoltas.empty())
        {
            c.modoVoltas = "unica";
        }
        if (c.tipo == "laps" && c.volta == 0 && c.especial.size() >= 2 && c.especial[0] == 'L')
        {
            c.volta = std::atoi(c.especial.c_str() + 1);
        }

        if (!c.inicio.empty() && !c.fim.empty())
        {
            int32_t ms = -1;
            std::string fmt;
            if (calcularDuracaoMs(c.inicio.c_str(), c.fim.c_str(), ms, fmt))
            {
                c.duracaoMs = ms;
                c.duracaoSegundos = ms / 1000;
                c.status = "OK";
                c.incompleto = false;
            }
            else
            {
                c.duracaoMs = -1;
                c.duracaoSegundos = -1;
                c.status = "invalido";
                c.incompleto = true;
            }
        }
        else if (c.inicio.empty() && !c.fim.empty())
        {
            c.duracaoMs = -1;
            c.duracaoSegundos = -1;
            c.status = "sem_largada";
            c.incompleto = true;
        }
        else if (!c.inicio.empty() && c.fim.empty())
        {
            c.duracaoMs = -1;
            c.duracaoSegundos = -1;
            c.status = "largada";
            c.incompleto = false;
        }
    }

    template <typename Container = std::deque<cronometragem_t>>
    static diagnostico_cronometragem_t diagnosticarLista(const Container &lista)
    {
        diagnostico_cronometragem_t diag;
        diag.totalRegistros = static_cast<int>(lista.size());
        for (const auto &item : lista)
        {
            if (item.status == "OK")
            {
                diag.concluidos++;
            }
            else if (item.status == "sem_largada")
            {
                diag.semLargada++;
            }
            else if (item.status == "invalido")
            {
                diag.invalidos++;
            }
            else if (item.status == "largada")
            {
                // L0 no modo voltas de estação única é o marco zero de largada (não é pendência de chegada)
                bool isLapZeroReference = (item.especial == "L0");
                if (!isLapZeroReference)
                {
                    diag.pendentesChegada++;
                }
            }
        }
        return diag;
    }

    static bool parseJsonItem(JsonObject cronometragemObj, cronometragem_t &outItem)
    {
        outItem = cronometragem_t{};
        outItem.uid = cronometragemObj["uid"].is<const char*>() ? cronometragemObj["uid"].as<const char*>() : "";
        outItem.numeral = cronometragemObj["numeral"].is<int>() ? cronometragemObj["numeral"].as<int>() : 0;
        outItem.especial = cronometragemObj["especial"].is<const char*>() ? cronometragemObj["especial"].as<const char*>() : "";
        if (cronometragemObj["tipo"].is<const char*>())
        {
            outItem.tipo = cronometragemObj["tipo"].as<const char*>();
        }
        else
        {
            outItem.tipo = (!outItem.especial.empty() && outItem.especial[0] == 'L') ? "laps" : "especial";
        }

        outItem.modoVoltas = cronometragemObj["modoVoltas"].is<const char*>()
                                 ? cronometragemObj["modoVoltas"].as<const char*>()
                                 : "unica";
        outItem.volta = cronometragemObj["volta"].is<int>() ? cronometragemObj["volta"].as<int>() : 0;
        outItem.inicio = cronometragemObj["inicio"].is<const char*>() ? cronometragemObj["inicio"].as<const char*>() : "";
        outItem.fim = cronometragemObj["fim"].is<const char*>() ? cronometragemObj["fim"].as<const char*>() : "";

        if ((outItem.uid.empty() && outItem.numeral <= 0) || outItem.especial.empty() || (outItem.inicio.empty() && outItem.fim.empty()))
        {
            return false;
        }

        preencherDerivados(outItem);
        return true;
    }

    static int formatItemJson(const cronometragem_t &reg, char *buf, size_t bufSize, bool appendNewline = false)
    {
        return std::snprintf(buf, bufSize,
            appendNewline
                ? "{\"uid\":\"%s\",\"numeral\":%d,\"tipo\":\"%s\",\"modoVoltas\":\"%s\",\"especial\":\"%s\",\"volta\":%d,\"inicio\":\"%s\",\"fim\":\"%s\",\"duracao\":\"%s\",\"duracaoSegundos\":%ld,\"duracaoMs\":%ld,\"status\":\"%s\",\"incompleto\":%s}\n"
                : "{\"uid\":\"%s\",\"numeral\":%d,\"tipo\":\"%s\",\"modoVoltas\":\"%s\",\"especial\":\"%s\",\"volta\":%d,\"inicio\":\"%s\",\"fim\":\"%s\",\"duracao\":\"%s\",\"duracaoSegundos\":%ld,\"duracaoMs\":%ld,\"status\":\"%s\",\"incompleto\":%s}",
            reg.uid.c_str(),
            static_cast<int>(reg.numeral),
            reg.tipo.c_str(),
            reg.modoVoltas.c_str(),
            reg.especial.c_str(),
            static_cast<int>(reg.volta),
            reg.inicio.c_str(),
            reg.fim.c_str(),
            reg.duracao().c_str(),
            static_cast<long>(reg.duracaoSegundos),
            static_cast<long>(reg.duracaoMs),
            reg.status.c_str(),
            reg.incompleto ? "true" : "false");
    }

    static void serializarItemToStream(const cronometragem_t &reg, Print &out)
    {
        char buf[320];
        int len = formatItemJson(reg, buf, sizeof(buf), false);
        if (len > 0 && static_cast<size_t>(len) < sizeof(buf))
        {
            out.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
        }
    }

    template <typename Container = std::deque<cronometragem_t>>
    static void serializarListaToStream(const Container &lista, Print &out)
    {
        out.write('[');
        bool first = true;
        for (const auto &reg : lista)
        {
            if (!first)
            {
                out.write(',');
            }
            first = false;
            serializarItemToStream(reg, out);
        }
        out.write(']');
    }

    static void serializarItem(const cronometragem_t &cronometragem, JsonObject &cronometragemObj)
    {
        cronometragemObj["uid"] = cronometragem.uid.c_str();
        cronometragemObj["numeral"] = cronometragem.numeral;
        cronometragemObj["tipo"] = cronometragem.tipo.c_str();
        cronometragemObj["modoVoltas"] = cronometragem.modoVoltas.c_str();
        cronometragemObj["especial"] = cronometragem.especial.c_str();
        cronometragemObj["volta"] = cronometragem.volta;
        cronometragemObj["inicio"] = cronometragem.inicio.c_str();
        cronometragemObj["fim"] = cronometragem.fim.c_str();
        cronometragemObj["duracao"] = cronometragem.duracao();
        cronometragemObj["duracaoSegundos"] = cronometragem.duracaoSegundos;
        cronometragemObj["duracaoMs"] = cronometragem.duracaoMs;
        cronometragemObj["status"] = cronometragem.status.c_str();
        cronometragemObj["incompleto"] = cronometragem.incompleto;
    }

    static bool mesmoRegistroChave(const cronometragem_t &a, const cronometragem_t &b)
    {
        // P2-02: Comparações inteiras rápidas antes de comparações textuais
        if (a.volta != b.volta)
        {
            return false;
        }
        if (a.numeral > 0 && b.numeral > 0 && a.numeral != b.numeral)
        {
            return false;
        }
        if (a.especial != b.especial)
        {
            return false;
        }
        if (!a.uid.empty() && !b.uid.empty())
        {
            return a.uid == b.uid;
        }
        if (a.numeral > 0 && b.numeral > 0)
        {
            return a.numeral == b.numeral;
        }
        return false;
    }

    static bool aplicarRegistro(std::deque<cronometragem_t> &destino, const cronometragem_t &item, cronometragem_t *outConsolidado = nullptr)
    {
        cronometragem_t registro = item;
        preencherDerivados(registro);

        for (auto &existing : destino)
        {
            if (!mesmoRegistroChave(existing, registro))
            {
                continue;
            }

            // Encontrou o registro correspondente
            // Caso 1: Evento de largada
            if (!registro.inicio.empty() && registro.fim.empty())
            {
                if (!existing.inicio.empty())
                {
                    // Já possui largada registrada, preserva o primeiro horário
                    if (outConsolidado) *outConsolidado = existing;
                    return false;
                }
                existing.inicio = registro.inicio;
                if (registro.numeral > 0) existing.numeral = registro.numeral;
                if (!registro.tipo.empty()) existing.tipo = registro.tipo;
                if (!registro.modoVoltas.empty()) existing.modoVoltas = registro.modoVoltas;
                if (registro.volta > 0) existing.volta = registro.volta;
                preencherDerivados(existing);
                if (outConsolidado) *outConsolidado = existing;
                return true;
            }

            // Caso 2: Evento de chegada
            if (!registro.fim.empty() && registro.inicio.empty())
            {
                if (!existing.fim.empty())
                {
                    // Não sobrescreve chegada já existente
                    if (outConsolidado) *outConsolidado = existing;
                    return false;
                }
                if (!existing.inicio.empty())
                {
                    registro.inicio = existing.inicio;
                }
                if (registro.numeral <= 0 && existing.numeral > 0)
                {
                    registro.numeral = existing.numeral;
                }
                if (registro.tipo.empty() && !existing.tipo.empty())
                {
                    registro.tipo = existing.tipo;
                }
                if (registro.modoVoltas.empty() && !existing.modoVoltas.empty())
                {
                    registro.modoVoltas = existing.modoVoltas;
                }
                preencherDerivados(registro);
                existing = registro;
                if (outConsolidado) *outConsolidado = existing;
                return true;
            }

            // Caso 3: Registro completo ou atualização de conciliação
            bool mudou = false;
            if (existing.inicio.empty() && !registro.inicio.empty())
            {
                existing.inicio = registro.inicio;
                mudou = true;
            }
            if (existing.fim.empty() && !registro.fim.empty())
            {
                existing.fim = registro.fim;
                mudou = true;
            }
            if (existing.numeral <= 0 && registro.numeral > 0)
            {
                existing.numeral = registro.numeral;
                mudou = true;
            }
            if (existing.uid.empty() && !registro.uid.empty())
            {
                existing.uid = registro.uid;
                mudou = true;
            }
            if (mudou)
            {
                preencherDerivados(existing);
            }
            if (outConsolidado) *outConsolidado = existing;
            return mudou;
        }

        // Não encontrado na lista: insere novo registro
        destino.push_back(registro);
        if (outConsolidado) *outConsolidado = registro;
        return true;
    }

    template <typename RemoteContainer = std::vector<cronometragem_t>>
    static bool mesclarLista(std::deque<cronometragem_t> &destino,
                             const RemoteContainer &remotos,
                             resultado_merge_sync_t &resultado,
                             bool sobrescreverConflitos = false)
    {
        bool houveAlteracao = false;
        resultado = resultado_merge_sync_t{};
        resultado.recebidos = static_cast<int>(remotos.size());

        for (const auto &rawRemoto : remotos)
        {
            cronometragem_t remoto = rawRemoto;
            if ((remoto.uid.empty() && remoto.numeral <= 0) || remoto.especial.empty() || (remoto.inicio.empty() && remoto.fim.empty()))
            {
                continue;
            }
            preencherDerivados(remoto);

            bool encontrado = false;
            for (auto &existing : destino)
            {
                // P2-02: Filtros rápidos inteiros antes de comparar UID
                if (remoto.volta != existing.volta)
                {
                    continue;
                }
                if (remoto.numeral > 0 && existing.numeral > 0 && remoto.numeral != existing.numeral)
                {
                    continue;
                }
                if (existing.especial != remoto.especial)
                {
                    continue;
                }
                if (!remoto.uid.empty() && !existing.uid.empty() && existing.uid != remoto.uid)
                {
                    continue;
                }
                if (existing.numeral <= 0 && remoto.numeral <= 0 && existing.uid.empty() && remoto.uid.empty())
                {
                    continue;
                }

                encontrado = true;
                bool alterouItem = false;
                bool conciliouHorarios = false;

                if (existing.uid.empty() && !remoto.uid.empty())
                {
                    existing.uid = remoto.uid;
                    alterouItem = true;
                }
                if (existing.numeral <= 0 && remoto.numeral > 0)
                {
                    existing.numeral = remoto.numeral;
                    alterouItem = true;
                }
                if (existing.volta <= 0 && remoto.volta > 0)
                {
                    existing.volta = remoto.volta;
                    alterouItem = true;
                }
                if (!remoto.tipo.empty() && existing.tipo.empty())
                {
                    existing.tipo = remoto.tipo;
                    alterouItem = true;
                }
                if (!remoto.modoVoltas.empty() && existing.modoVoltas == "unica" && remoto.modoVoltas == "duas")
                {
                    existing.modoVoltas = remoto.modoVoltas;
                    alterouItem = true;
                }

                bool conflitoInicio = !existing.inicio.empty() && !remoto.inicio.empty() && existing.inicio != remoto.inicio;
                bool conflitoFim = !existing.fim.empty() && !remoto.fim.empty() && existing.fim != remoto.fim;

                if (conflitoInicio || conflitoFim)
                {
                    conflito_sync_t conf;
                    conf.uid = !existing.uid.empty() ? existing.uid : remoto.uid;
                    conf.numeral = existing.numeral > 0 ? existing.numeral : remoto.numeral;
                    conf.especial = existing.especial;
                    if (conflitoInicio && conflitoFim)
                    {
                        conf.campo = "inicio/fim";
                        conf.valorLocal = existing.inicio + " -> " + existing.fim;
                        conf.valorRemoto = remoto.inicio + " -> " + remoto.fim;
                    }
                    else if (conflitoInicio)
                    {
                        conf.campo = "inicio";
                        conf.valorLocal = existing.inicio.c_str();
                        conf.valorRemoto = remoto.inicio.c_str();
                    }
                    else
                    {
                        conf.campo = "fim";
                        conf.valorLocal = existing.fim.c_str();
                        conf.valorRemoto = remoto.fim.c_str();
                    }
                    resultado.conflitos.push_back(conf);
                    resultado.conflitosDetectados++;

                    if (sobrescreverConflitos)
                    {
                        if (conflitoInicio)
                        {
                            existing.inicio = remoto.inicio;
                        }
                        if (conflitoFim)
                        {
                            existing.fim = remoto.fim;
                        }
                        alterouItem = true;
                        conciliouHorarios = true;
                    }
                }

                // Conciliação de largada ausente localmente
                if (existing.inicio.empty() && !remoto.inicio.empty())
                {
                    existing.inicio = remoto.inicio;
                    alterouItem = true;
                    conciliouHorarios = true;
                }

                // Conciliação de chegada ausente localmente
                if (existing.fim.empty() && !remoto.fim.empty())
                {
                    existing.fim = remoto.fim;
                    alterouItem = true;
                    conciliouHorarios = true;
                }

                if (alterouItem)
                {
                    preencherDerivados(existing);
                    houveAlteracao = true;
                    if (conciliouHorarios)
                    {
                        resultado.conciliados++;
                    }
                }
                else if (!conflitoInicio && !conflitoFim)
                {
                    resultado.duplicadosIgnorados++;
                }

                break;
            }

            if (!encontrado)
            {
                destino.push_back(remoto);
                resultado.inseridos++;
                houveAlteracao = true;
            }
        }

        resultado.diagnostico = diagnosticarLista(destino);
        return houveAlteracao;
    }

    static void read(Cronometragem &settings, JsonObject &root)
    {
        JsonArray cronometragens = root["cronometragem"].to<JsonArray>();
        for (const auto &cronometragem : settings.cronometragens)
        {
            JsonObject cronometragemObj = cronometragens.add<JsonObject>();
            serializarItem(cronometragem, cronometragemObj);
        }
    }

    static StateUpdateResult update(JsonObject &root, Cronometragem &cronometragem)
    {
        if (root["cronometragem"].is<JsonArray>())
        {
            cronometragem.cronometragens.clear();
            for (JsonVariant item : root["cronometragem"].as<JsonArray>())
            {
                if (item.is<JsonObject>())
                {
                    cronometragem_t c;
                    if (parseJsonItem(item.as<JsonObject>(), c))
                    {
                        cronometragem.cronometragens.push_back(c);
                    }
                }
            }
            return StateUpdateResult::CHANGED;
        }
        return StateUpdateResult::UNCHANGED;
    }
};

class CronometragemService : public StatefulService<Cronometragem>
{
public:
    CronometragemService(PsychicHttpServer *server,
                         ESP32SvelteKit *sveltekit, FS *fs);

    void begin();
    void registrarTempo(const cronometragem_t &novo);
    bool appendRegistroToFS(const cronometragem_t &reg);
    bool saveToFS();
    bool readFromFS();
    bool jaRegistrado(const std::string &uid, const std::string &especial, const std::string &posicao);
    bool obterHorarioInicio(const std::string &uid, const std::string &especial, std::string &outInicio);

    // Métodos auxiliares para o Modo Voltas (RF-10 / BL-08)
    bool obterUltimaPassagemVoltaUnica(const std::string &uid, std::string &outUltimoHorario, int &outProximaVolta);
    bool obterEstadoVoltaDuasEstacoes(const std::string &uid, int &outUltimaVolta, bool &outTemLargadaAberta, std::string &outHorarioLargadaAberta);

    // Métodos de Sincronização Final e Diagnóstico (RF-12 / BL-11)
    resultado_merge_sync_t consolidarRegistros(const std::vector<cronometragem_t> &remotos, bool sobrescreverConflitos = false);
    resultado_merge_sync_t consolidarRegistros(const std::deque<cronometragem_t> &remotos, bool sobrescreverConflitos = false);
    diagnostico_cronometragem_t obterDiagnostico();

private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    FS *_fs;
    FSPersistence<Cronometragem> _fsPersistence;
    HttpEndpoint<Cronometragem> _httpEndpoint;
    SemaphoreHandle_t _fsMutex{nullptr};

    esp_err_t sendCronometragemStream(PsychicRequest *request);
    esp_err_t handleGetCronometragem(PsychicRequest *request);
    esp_err_t handlePostCronometragem(PsychicRequest *request, JsonVariant &json);

    void registerConfig();
    void onConfigUpdated(const String &originId);
};

#endif
