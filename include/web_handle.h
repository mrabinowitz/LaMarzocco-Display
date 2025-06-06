#pragma once

#include <WebServer.h>
#include "config.h"

void initFS(void);
void sendSSID(void);
void cssHandler(void);
void mainHandler(void);
void handleNotFound(void);
void saveWifiHandler(void);
void saveCloudHandler(void);
void saveMachineHandler(void);