SET(SOURCES ui_welcomeScreen.c
    ui_NoConnectionScreen.c
    ui_setupWifiScreen.c
    ui_mainScreen.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_wifi0_png.c
    ui_img_cross_png.c
    ui_img_battery1_png.c
    ui_img_battery0_png.c
    ui_img_power_png.c
    ui_img_steam_png.c
    ui_img_coffee_png.c
    ui_img_wind_png.c
    ui_img_drop_2_png.c
    ui_img_battery2_png.c
    ui_img_battery3_png.c
    ui_img_wifi1_png.c
    ui_img_wifi2_png.c
    ui_img_wifi3_png.c)

add_library(ui ${SOURCES})
