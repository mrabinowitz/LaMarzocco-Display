#include "update_screen.h"
#include "Arduino.h"
#include "time.h"
#include <ui/ui.h>

unsigned long timeUpdate = 0;

bool updateDateTime(void)
{
    if ((millis() - timeUpdate <= TIME_UPDATE))
        return false;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        log_e("Failed to obtain time");
        return false;
    }

    char timeStr[32];

    snprintf(timeStr, sizeof(timeStr), "%02d : %02d",
             timeinfo.tm_hour,
             timeinfo.tm_min);

    lv_label_set_text(ui_timeLabel, timeStr);

    timeUpdate = millis();
    return true;
}