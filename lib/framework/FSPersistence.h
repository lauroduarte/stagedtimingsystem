#ifndef FSPersistence_h
#define FSPersistence_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 - 2024 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <StatefulService.h>
#include <FS.h>
#include <esp_log.h>

template <class T>
class FSPersistence
{
public:
    FSPersistence(JsonStateReader<T> stateReader,
                  JsonStateUpdater<T> stateUpdater,
                  StatefulService<T> *statefulService,
                  FS *fs,
                  const char *filePath) : _stateReader(stateReader),
                                          _stateUpdater(stateUpdater),
                                          _statefulService(statefulService),
                                          _fs(fs),
                                          _filePath(filePath),
                                          _updateHandlerId(0)
    {
        enableUpdateHandler();
    }

    void readFromFS()
    {
        String tmpPath = String(_filePath) + ".tmp";
        String bakPath = String(_filePath) + ".bak";

        File settingsFile = _fs->open(_filePath, "r");
        if (settingsFile)
        {
            JsonDocument jsonDocument;
            DeserializationError error = deserializeJson(jsonDocument, settingsFile);
            settingsFile.close();

            if (error == DeserializationError::Ok && jsonDocument.is<JsonObject>())
            {
                JsonObject jsonObject = jsonDocument.as<JsonObject>();
                _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);

                // Limpa .tmp residual caso tenha ficado de crash/reset anterior
                if (_fs->exists(tmpPath.c_str()))
                {
                    _fs->remove(tmpPath.c_str());
                }
                return;
            }
            else
            {
                // P0-02: Arquivo corrompido! Renomeia para .bak antes de sobrescrever com defaults
                if (_fs->exists(bakPath.c_str()))
                {
                    _fs->remove(bakPath.c_str());
                }
                _fs->rename(_filePath, bakPath.c_str());
                ESP_LOGE("FSPersist", "Arquivo %s corrompido! Salvo em %s", _filePath, bakPath.c_str());
            }
        }

        // P0-02: Se o arquivo principal não pôde ser lido ou estava corrompido, tenta recuperar do .tmp
        if (_fs->exists(tmpPath.c_str()))
        {
            File tmpFile = _fs->open(tmpPath.c_str(), "r");
            if (tmpFile)
            {
                JsonDocument jsonDocument;
                DeserializationError error = deserializeJson(jsonDocument, tmpFile);
                tmpFile.close();

                if (error == DeserializationError::Ok && jsonDocument.is<JsonObject>())
                {
                    if (!_fs->rename(tmpPath.c_str(), _filePath))
                    {
                        _fs->remove(_filePath);
                        _fs->rename(tmpPath.c_str(), _filePath);
                    }
                    JsonObject jsonObject = jsonDocument.as<JsonObject>();
                    _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);
                    ESP_LOGW("FSPersist", "Recuperado arquivo valido de %s para %s", tmpPath.c_str(), _filePath);
                    return;
                }
                else
                {
                    _fs->remove(tmpPath.c_str());
                }
            }
        }

        // If we reach here we have not been successful in loading the config and hard-coded defaults are now applied.
        // The settings are then written back to the file system so the defaults persist between resets. This last step is
        // required as in some cases defaults contain randomly generated values which would otherwise be modified on reset.
        applyDefaults();
        writeToFS();
    }

    bool writeToFS()
    {
        // create and populate a new json object
        JsonDocument jsonDocument;
        JsonObject jsonObject = jsonDocument.to<JsonObject>();
        _statefulService->read(jsonObject, _stateReader);

        // P0-02: aborta imediatamente se o documento estourou capacidade (evita corromper arquivo)
        if (jsonDocument.overflowed())
        {
            ESP_LOGE("FSPersist", "JsonDocument overflowed ao salvar %s! Gravacao abortada.", _filePath);
            return false;
        }

        // make directories if required
        mkdirs();

        String tmpPath = String(_filePath) + ".tmp";

        // P0-02: Escrita atômica em arquivo temporário .tmp
        File settingsFile = _fs->open(tmpPath.c_str(), "w");
        if (!settingsFile)
        {
            ESP_LOGE("FSPersist", "Falha ao abrir %s para escrita temporaria.", tmpPath.c_str());
            return false;
        }

        size_t bytesWritten = serializeJson(jsonDocument, settingsFile);
        settingsFile.flush();
        settingsFile.close();

        if (bytesWritten == 0)
        {
            _fs->remove(tmpPath.c_str());
            ESP_LOGE("FSPersist", "Zero bytes escritos em %s.", tmpPath.c_str());
            return false;
        }

        // Rename atômico para o arquivo definitivo
        if (!_fs->rename(tmpPath.c_str(), _filePath))
        {
            _fs->remove(_filePath);
            if (!_fs->rename(tmpPath.c_str(), _filePath))
            {
                _fs->remove(tmpPath.c_str());
                ESP_LOGE("FSPersist", "Falha ao renomear %s para %s", tmpPath.c_str(), _filePath);
                return false;
            }
        }

        return true;
    }

    void disableUpdateHandler()
    {
        if (_updateHandlerId)
        {
            _statefulService->removeUpdateHandler(_updateHandlerId);
            _updateHandlerId = 0;
        }
    }

    void enableUpdateHandler()
    {
        if (!_updateHandlerId)
        {
            _updateHandlerId = _statefulService->addUpdateHandler([&](const String &originId)
                                                                  { writeToFS(); });
        }
    }

    const char *getFilePath() const
    {
        return _filePath;
    }

    FS *getFS() const
    {
        return _fs;
    }

private:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    FS *_fs;
    const char *_filePath;
    update_handler_id_t _updateHandlerId;

    // We assume we have a _filePath with format "/directory1/directory2/filename"
    // We create a directory for each missing parent
    void mkdirs()
    {
        String path(_filePath);
        int index = 0;
        while ((index = path.indexOf('/', index + 1)) != -1)
        {
            String segment = path.substring(0, index);
            if (!_fs->exists(segment))
            {
                _fs->mkdir(segment);
            }
        }
    }

protected:
    // We assume the updater supplies sensible defaults if an empty object
    // is supplied, this virtual function allows that to be changed.
    virtual void applyDefaults()
    {
        JsonDocument jsonDocument;
        JsonObject jsonObject = jsonDocument.as<JsonObject>();
        _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);
    }
};

#endif // end FSPersistence