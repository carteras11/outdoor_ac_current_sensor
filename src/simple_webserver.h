#ifndef SIMPLE_WEBSERVER_H
#define SIMPLE_WEBSERVER_H

#include <WebServer.h>

// Initialize web server
void initWebServer();

// Start web server
void startWebServer();

// Handle client requests (call in loop)
void handleWebClient();

// Get web server object
WebServer& getWebServer();

#endif