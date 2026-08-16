#pragma once

#include <LovyanGFX.hpp>
#include "config/config.h"

// Unified display driver: SPI (320x240 DevKitC wiring) or 8-bit parallel 8080
// (LilyGo T-Display-S3, 320x170 landscape). Selected via -D DISPLAY_8080.
class LGFX : public lgfx::LGFX_Device {
#if defined(DISPLAY_8080)
    lgfx::Bus_Parallel8 _bus;
#else
    lgfx::Bus_SPI _bus;
#endif
    lgfx::Panel_ST7789 _panel;

public:
    LGFX(void) {
        {
            auto cfg = _bus.config();
#if defined(DISPLAY_8080)
            cfg.freq_write = 16000000;
            cfg.freq_read = 8000000;
            cfg.pin_rd = 9;
            cfg.pin_wr = 8;
            cfg.pin_rs = 7;
            cfg.pin_d0 = 39; cfg.pin_d1 = 40; cfg.pin_d2 = 41; cfg.pin_d3 = 42;
            cfg.pin_d4 = 45; cfg.pin_d5 = 46; cfg.pin_d6 = 47; cfg.pin_d7 = 48;
#else
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 60000000;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = PIN_LCD_SCK;
            cfg.pin_mosi = PIN_LCD_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc = PIN_LCD_DC;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
#endif
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_LCD_CS;
            cfg.pin_rst = PIN_LCD_RST;
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
#if defined(DISPLAY_8080)
            cfg.panel_width = 170;
            cfg.panel_height = 320;
            cfg.offset_x = 35;
            cfg.offset_y = 0;
#else
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
#endif
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
#if defined(DISPLAY_8080)
            cfg.invert = true;    // T-Display-S3 ST7789 needs INVON (else white bg / inverted colors)
#else
            cfg.invert = false;   // SPI board uses INVOFF
#endif
            cfg.rgb_order = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

// Adafruit-compatible RGB565 color aliases (so existing code keeps working)
#ifndef ST77XX_BLACK
#define ST77XX_BLACK   0x0000
#define ST77XX_WHITE   0xFFFF
#define ST77XX_CYAN    0x07FF
#define ST77XX_GREEN   0x07E0
#define ST77XX_RED     0xF800
#define ST77XX_MAGENTA 0xF81F
#endif
