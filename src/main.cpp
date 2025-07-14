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

#include <ESP32SvelteKit.h>
#include <LightMqttSettingsService.h>
#include <LightStateService.h>
#include <PsychicHttpServer.h>
#include <ProvaService.h>
#include <CategoriaService.h>
#include <AtletaService.h>
#include <RelogioService.h>
#include <RFIDService.h>

#define SERIAL_BAUD_RATE 115200

PsychicHttpServer server;

ESP32SvelteKit esp32sveltekit(&server, 120);

LightMqttSettingsService lightMqttSettingsService = LightMqttSettingsService(&server,
                                                                             &esp32sveltekit);

LightStateService lightStateService = LightStateService(&server,
                                                        &esp32sveltekit,
                                                        &lightMqttSettingsService);

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

RFIDService rfidService = RFIDService(&server,
                                                        &esp32sveltekit,
                                                        esp32sveltekit.getSecurityManager());  
                                                

void setup()
{
    // start serial and filesystem
    Serial.begin(SERIAL_BAUD_RATE);

    // start ESP32-SvelteKit
    esp32sveltekit.begin();

    // load the initial light settings
    lightStateService.begin();
    // start the light service
    lightMqttSettingsService.begin();

    // start the prova service
    provaService.begin();

    // start the categoria service
    categoriaService.begin();

    // start the atleta service
    atletaService.begin();

    // start the relogio service
    relogioService.begin();

    // start the RFID service
    rfidService.begin();
}

void loop()
{
    rfidService.loop();
    // Delete Arduino loop task, as it is not needed in this example
    vTaskDelete(NULL);
}
