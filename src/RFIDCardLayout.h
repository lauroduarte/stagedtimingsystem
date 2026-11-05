#ifndef RFIDCardLayout_h
#define RFIDCardLayout_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace RFIDCard {

static constexpr uint8_t MAGIC_0 = 0x53; // 'S'
static constexpr uint8_t MAGIC_1 = 0x54; // 'T'
static constexpr uint8_t FORMAT_VERSION = 1;

static constexpr uint8_t HEADER_BLOCK = 4;
static constexpr uint8_t MAX_RECORDS = 44; // 44 blocos de dados úteis (5..62, exceto trailers)

enum Mode : uint8_t {
    MODE_NONE = 0,
    MODE_ESTAGIO = 1,
    MODE_VOLTA_UNICA = 2,
    MODE_VOLTA_DUAS = 3
};

enum Flags : uint8_t {
    FLAG_NONE = 0x00,
    FLAG_START = 0x01,
    FLAG_FINISH = 0x02,
    FLAG_COMPLETE = (FLAG_START | FLAG_FINISH)
};

#pragma pack(push, 1)
struct Header {
    uint8_t magic0;       // MAGIC_0 (0x53)
    uint8_t magic1;       // MAGIC_1 (0x54)
    uint8_t version;      // FORMAT_VERSION (1)
    uint8_t mode;         // Mode enum (1: Estágio, 2: Volta Única, 3: Volta Duas)
    uint8_t uidBytes[4];  // 4 primeiros bytes do UID
    uint8_t recordCount;  // Quantidade de registros armazenados (0 a MAX_RECORDS)
    uint8_t flags;        // Flags adicionais do cartão
    uint8_t reserved[5];  // Reservados para expansão futura (zeros)
    uint8_t crc8;         // CRC-8 dos primeiros 15 bytes
};
static_assert(sizeof(Header) == 16, "RFIDCard::Header deve ter exatamente 16 bytes");

struct Record {
    uint16_t id;          // Número do Estágio (1..10) ou da Volta (0..44)
    uint8_t flags;        // Flags (FLAG_START, FLAG_FINISH, FLAG_COMPLETE)
    uint32_t tsInicio;    // Segundos do dia da largada / início (0 se ausente)
    uint32_t tsFim;       // Segundos do dia da chegada / fim (0 se pendente)
    uint16_t msInicio;    // Subsegundos ms da largada (0..999)
    uint16_t msFim;       // Subsegundos ms da chegada (0..999)
    uint8_t crc8;         // CRC-8 dos primeiros 15 bytes
};
static_assert(sizeof(Record) == 16, "RFIDCard::Record deve ter exatamente 16 bytes");
#pragma pack(pop)

// Cálculo determinístico de CRC-8 (polinômio 0x07, padrão SMBus/Dallas)
inline uint8_t calcCRC8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    while (len--)
    {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x80)
            {
                crc = (crc << 1) ^ 0x07;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Mapeia o índice do registro (0 a 43) para o bloco absoluto MIFARE (5 a 62), pulando trailers de setor
inline uint8_t recordIndexToBlock(uint8_t recordIndex)
{
    // dataBlock 0 é o Header (bloco 4).
    // dataBlock 1..44 correspondem aos Records 0..43.
    uint8_t dataBlock = 1 + recordIndex;
    uint8_t sector = 1 + (dataBlock / 3);
    uint8_t relBlock = dataBlock % 3;
    return (sector * 4) + relBlock;
}

// Verifica se um buffer de 16 bytes está zerado (cartão recém-resetado)
inline bool isBlank(const uint8_t *buf16)
{
    for (int i = 0; i < 16; i++)
    {
        if (buf16[i] != 0x00)
        {
            return false;
        }
    }
    return true;
}

// Valida a integridade do cabeçalho binário do cartão
inline bool isValidHeader(const Header &h)
{
    if (h.magic0 != MAGIC_0 || h.magic1 != MAGIC_1 || h.version != FORMAT_VERSION)
    {
        return false;
    }
    if (h.recordCount > MAX_RECORDS)
    {
        return false;
    }
    uint8_t expectedCrc = calcCRC8(reinterpret_cast<const uint8_t*>(&h), 15);
    return (h.crc8 == expectedCrc);
}

// Valida a integridade de um registro individual de 16 bytes
inline bool isValidRecord(const Record &r)
{
    uint8_t expectedCrc = calcCRC8(reinterpret_cast<const uint8_t*>(&r), 15);
    return (r.crc8 == expectedCrc);
}

// Converte string "HH:MM:SS" para segundos do dia (0 a 86399)
inline uint32_t timeStringToSeconds(const String &timeStr)
{
    if (timeStr.length() < 8)
    {
        return 0;
    }
    int h = (timeStr[0] - '0') * 10 + (timeStr[1] - '0');
    int m = (timeStr[3] - '0') * 10 + (timeStr[4] - '0');
    int s = (timeStr[6] - '0') * 10 + (timeStr[7] - '0');
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
    {
        return 0;
    }
    return static_cast<uint32_t>(h * 3600 + m * 60 + s);
}

// Converte segundos do dia para string formatada "HH:MM:SS"
inline String secondsToTimeString(uint32_t seconds)
{
    char buf[12];
    uint32_t s = seconds % 86400;
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int sec = s % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, sec);
    return String(buf);
}

// Decodifica os registros binários do cartão para o formato JsonDocument padrão do sistema
inline void decodeBinaryCardToJson(const String &uid, const Header &h, const std::vector<Record> &records, JsonDocument &doc)
{
    doc.clear();
    doc["u"] = uid;

    if (h.mode == MODE_ESTAGIO)
    {
        for (const auto &rec : records)
        {
            if (rec.id == 0 || rec.id > MAX_RECORDS || rec.flags == 0)
            {
                continue;
            }
            String stageKey = "E" + String(rec.id);
            if (rec.flags & FLAG_START)
            {
                doc["e"][stageKey]["i"] = secondsToTimeString(rec.tsInicio);
            }
            if (rec.flags & FLAG_FINISH)
            {
                doc["e"][stageKey]["f"] = secondsToTimeString(rec.tsFim);
            }
        }
    }
    else if (h.mode == MODE_VOLTA_UNICA)
    {
        JsonArray lapsArr = doc["l"].to<JsonArray>();
        for (const auto &rec : records)
        {
            if (rec.flags == 0)
            {
                continue;
            }
            if (rec.flags & FLAG_START)
            {
                lapsArr.add(secondsToTimeString(rec.tsInicio));
            }
        }
    }
    else if (h.mode == MODE_VOLTA_DUAS)
    {
        JsonArray voltasArr = doc["v"].to<JsonArray>();
        for (const auto &rec : records)
        {
            if (rec.id == 0 || rec.id > MAX_RECORDS || rec.flags == 0)
            {
                continue;
            }
            while (voltasArr.size() < static_cast<size_t>(rec.id - 1))
            {
                voltasArr.add<JsonObject>();
            }
            JsonObject obj = (voltasArr.size() == static_cast<size_t>(rec.id - 1))
                                 ? voltasArr.add<JsonObject>()
                                 : voltasArr[rec.id - 1].as<JsonObject>();
            if (rec.flags & FLAG_START)
            {
                obj["i"] = secondsToTimeString(rec.tsInicio);
            }
            if (rec.flags & FLAG_FINISH)
            {
                obj["f"] = secondsToTimeString(rec.tsFim);
            }
        }
    }
}

} // namespace RFIDCard

#endif // RFIDCardLayout_h
