// =============================================================================
//  web_ui.h  –  Bootstrap web UI handler
//  Source: https://raw.githubusercontent.com/baghamut/Winery-Controller/main/web_ui.h
// =============================================================================
#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Register GET / (bootstrap redirect) and OPTIONS / (CORS preflight).
// Call once from httpServerInit().
void webUiRegisterHandlers(WebServer& server);

// Add Access-Control-Allow-Origin: * to any response.
// Call from http_server.cpp before server.send() on /state.
void webUiAddCorsHeaders(WebServer& server);