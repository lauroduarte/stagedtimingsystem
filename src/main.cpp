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

#include <WiFi.h>
#include <ESP32SvelteKit.h>
#include <PsychicHttpServer.h>
#include <ProvaService.h>
#include <CategoriaService.h>
#include <AtletaService.h>
#include <RelogioService.h>
#include <RFIDService.h>
#include <DisplayService.h>
#include <BuzzerService.h>
#include <CronometragemService.h>
#include <CronometrarService.h>
#include <SyncService.h>

#define SERIAL_BAUD_RATE 115200

PsychicHttpServer server;

ESP32SvelteKit esp32sveltekit(&server, 150);

ProvaService provaService = ProvaService(&server,
                                                        &esp32sveltekit,
                                                        esp32sveltekit.getFS());                                                        


CategoriaService categoriaService = CategoriaService(&server,
                                                        &esp32sveltekit,
                                                        esp32sveltekit.getFS());

AtletaService atletaService = AtletaService(&server,
                                                        &esp32sveltekit,
                                                        esp32sveltekit.getFS());         
                                                        
RelogioService relogioService = RelogioService(&server,
                                                        &esp32sveltekit,
                                                        esp32sveltekit.getFS());

DisplayService displayService = DisplayService();

BuzzerService buzzerService = BuzzerService();

RFIDService rfidService = RFIDService(&server,
                                                        &esp32sveltekit,
                                                        esp32sveltekit.getSecurityManager(),
                                                        &displayService,
                                                        esp32sveltekit.getFS(),
                                                        &buzzerService);  

CronometragemService cronometragemService = CronometragemService(&server,
                                                                &esp32sveltekit,
                                                                esp32sveltekit.getFS());

CronometrarService cronometrarService = CronometrarService(&server,
                                                          &esp32sveltekit,
                                                          esp32sveltekit.getFS(),
                                                          rfidService.getRFID(),
                                                          &buzzerService,
                                                          &displayService,
                                                          &cronometragemService,
                                                          &rfidService,
                                                          &atletaService,
                                                          &provaService);

SyncService syncService = SyncService(&server,
                                      &esp32sveltekit,
                                      &provaService,
                                      &categoriaService,
                                      &atletaService,
                                      &rfidService,
                                      &relogioService,
                                      &cronometrarService,
                                      &cronometragemService);

void setup()
{
    // start serial and filesystem
    Serial.begin(SERIAL_BAUD_RATE);
    while(!Serial);

    // Evita modem sleep do WiFi para estabilidade da conexão e baixa latência
    WiFi.setSleep(false);

    // start ESP32-SvelteKit
    server.config.stack_size = 24576;
    server.config.lru_purge_enable = true;
    server.config.recv_wait_timeout = 5;
    server.config.send_wait_timeout = 5;
#ifdef CONFIG_LWIP_MAX_SOCKETS
    server.config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS > 3)
                                         ? ((CONFIG_LWIP_MAX_SOCKETS - 3 > 12) ? 12 : (CONFIG_LWIP_MAX_SOCKETS - 3))
                                         : 7;
#endif
    esp32sveltekit.begin();

    // start the prova service
    provaService.begin();

    // start the categoria service
    categoriaService.begin();

    // start the atleta service
    atletaService.begin();

    // start the relogio service
    relogioService.begin();

    // start the display service
    displayService.begin();

    // start the buzzer service
    buzzerService.begin();

    // exclusão mútua entre Modo Reset e Modo Cronometragem (BL-01)
    rfidService.setOnResetStartedCallback([]() {
        cronometrarService.stopCronometragem();
    });
    rfidService.setIsCronometrandoCallback([]() {
        return cronometrarService.isCronometrando();
    });

    // Bloqueia alteração do relógio e varredura Wi-Fi STA durante cronometragem ativa (P3-02, P1-05)
    relogioService.setIsCronometrandoCallback([]() {
        return cronometrarService.isCronometrando();
    });
    esp32sveltekit.getWiFiSettingsService()->setCronometrandoCallback([]() {
        return cronometrarService.isCronometrando();
    });

    // start the RFID service
    rfidService.begin();

    // start the cronometragem service
    cronometragemService.begin();

    // start the cronometrar service
    cronometrarService.begin();

    // start the sync service (RF-06, RF-07, RF-12 / BL-10, BL-11)
    syncService.begin();
}

void loop()
{
    buzzerService.loop();
    displayService.loop();
    relogioService.loop();
    if (!rfidService.isResetMode() && !displayService.isAnimating())
    {
        static unsigned long lastClockUpdateMs = 0;
        unsigned long nowMs = millis();
        if (nowMs - lastClockUpdateMs >= 1000)
        {
            lastClockUpdateMs = nowMs;
            displayService.showClock(Relogio::getNow());
        }
    }
    rfidService.loop();
    cronometrarService.loop();
    delay(10);
}
