#pragma once

#include <WebServer.h>
#include "config.h"

void initFS(void);
void sendSSID(void);
void sendStatus(void);
void cssHandler(void);
void mainHandler(void);
void restartHander(void);
void handleNotFound(void);
void saveWifiHandler(void);
void saveCloudHandler(void);
void saveMachineHandler(void);