#pragma once
#include <MD_MAX72XX.h>

// Hardware SPI pins — adjust if needed
#define DISP_DIN_PIN  8
#define DISP_CLK_PIN  9   // hardware SPI SCK
#define DISP_CS_PIN   10

#define MAX_DEVICES   1

// 3×5 pixel font for digits 0-9.
// Each entry is 3 bytes representing 3 columns (bit 0 = top row, bit 4 = bottom row).
// Column order: leftmost → rightmost.
static const uint8_t DIGIT_FONT[10][3] = {
    {0x1F, 0x11, 0x1F}, // 0
    {0x00, 0x1F, 0x00}, // 1
    {0x1D, 0x15, 0x17}, // 2
    {0x11, 0x15, 0x1F}, // 3
    {0x07, 0x04, 0x1F}, // 4
    {0x17, 0x15, 0x1D}, // 5
    {0x1F, 0x15, 0x1D}, // 6
    {0x01, 0x01, 0x1F}, // 7
    {0x1F, 0x15, 0x1F}, // 8
    {0x17, 0x15, 0x1F}, // 9
};

// Dash character: single centre row  "---"
static const uint8_t DASH_COL = 0x04; // bit 2 only (middle of 5 rows)

struct Display {
    MD_MAX72XX mx;
    Display() : mx(MD_MAX72XX::FC16_HW, DISP_CS_PIN, MAX_DEVICES) {}
};

inline void display_init(Display &d) {
    d.mx.begin();
    d.mx.control(MD_MAX72XX::INTENSITY, 8);
    d.mx.clear();
}

// Write a digit (0–9) into columns col..col+2 (3 wide), centred vertically in 8 rows.
// Row offset: (8 - 5) / 2 = 1 → rows 1..5 used.
static void _write_digit(Display &d, uint8_t digit, uint8_t start_col) {
    for (uint8_t c = 0; c < 3; c++) {
        uint8_t col_data = DIGIT_FONT[digit][c];
        // shift up by 1 row to centre 5-row digit in 8-row matrix
        d.mx.setColumn(0, start_col + c, col_data << 1);
    }
}

// Show two digits (00–99) side-by-side: left digit at col 0, gap at col 3, right digit at col 4.
// Total: 3 + 1 + 3 = 7 cols, leaving 1 col border on right.
inline void display_show_seconds(Display &d, int seconds) {
    seconds = constrain(seconds, 0, 99);
    d.mx.clear();
    _write_digit(d, seconds / 10, 0);
    _write_digit(d, seconds % 10, 4);
    d.mx.update();
}

// Show "--" idle indicator
inline void display_show_idle(Display &d) {
    d.mx.clear();
    d.mx.setColumn(0, 0, DASH_COL << 1);
    d.mx.setColumn(0, 1, DASH_COL << 1);
    d.mx.setColumn(0, 2, DASH_COL << 1);
    d.mx.setColumn(0, 4, DASH_COL << 1);
    d.mx.setColumn(0, 5, DASH_COL << 1);
    d.mx.setColumn(0, 6, DASH_COL << 1);
    d.mx.update();
}

inline void display_set_brightness(Display &d, uint8_t level) {
    d.mx.control(MD_MAX72XX::INTENSITY, level); // 0–15
}

// Flash the result on/off; call repeatedly. Returns true after flash_count flashes.
inline bool display_flash_result(Display &d, int seconds, uint8_t &flash_count, unsigned long &last_ms) {
    unsigned long now = millis();
    if (now - last_ms < 500) return false;
    last_ms = now;
    if (flash_count % 2 == 0)
        display_show_seconds(d, seconds);
    else {
        d.mx.clear();
        d.mx.update();
    }
    flash_count++;
    return flash_count >= 10; // 5 on/off cycles
}
