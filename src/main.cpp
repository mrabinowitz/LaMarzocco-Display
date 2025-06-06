#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <ui/ui.h>
#include "Preferences.h"

Preferences preferences;

LilyGo_Class amoled;

void setup()
{
  Serial.begin(115200);
  preferences.begin("config", false);

  bool rslt = false;

  // Automatically determine the access device
  rslt = amoled.begin();

  if (!rslt)
  {
    while (1)
    {
      Serial.print("The board model cannot be detected, please raise the Core Debug Level to an error");
      delay(1000);
    }
  }
  beginLvglHelper(amoled);
  ui_init();
}

void loop()
{
  lv_task_handler();
  delay(5);
}
