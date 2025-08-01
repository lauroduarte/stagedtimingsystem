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

#include <LightStateService.h>

LightStateService::LightStateService(PsychicHttpServer *server,
                                     ESP32SvelteKit *sveltekit) : _httpEndpoint(LightState::read,
                                                                                                         LightState::update,
                                                                                                         this,
                                                                                                         server,
                                                                                                         LIGHT_SETTINGS_ENDPOINT_PATH,
                                                                                                         sveltekit->getSecurityManager(),
                                                                                                         AuthenticationPredicates::IS_AUTHENTICATED),
                                                                                           _webSocketServer(LightState::read,
                                                                                                            LightState::update,
                                                                                                            this,
                                                                                                            server,
                                                                                                            LIGHT_SETTINGS_SOCKET_PATH,
                                                                                                            sveltekit->getSecurityManager(),
                                                                                                            AuthenticationPredicates::IS_AUTHENTICATED)
{
    // configure led to be output
    pinMode(LED_BUILTIN, OUTPUT);



    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void LightStateService::begin()
{
    _httpEndpoint.begin();
    _state.ledOn = DEFAULT_LED_STATE;
    onConfigUpdated();
}

void LightStateService::onConfigUpdated()
{
    digitalWrite(LED_BUILTIN, _state.ledOn ? 1 : 0);
}

void LightStateService::registerConfig()
{

}
