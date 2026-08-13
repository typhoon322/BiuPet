#pragma once

#include <LovyanGFX.hpp>
#include "config/config.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _bus_instance;
    lgfx::Panel_ST7789 _panel_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 60000000;   // MusicGoGoGo uses 60 MHz
            cfg.freq_read = 16000000;
            cfg.pin_sclk = PIN_LCD_SCK;
            cfg.pin_mosi = PIN_LCD_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc = PIN_LCD_DC;
            cfg.spi_3wire = false;       // 4-wire SPI, DC is a separate pin
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            _bus_instance.config(cfg);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = PIN_LCD_CS;
            cfg.pin_rst = PIN_LCD_RST;
            cfg.pin_busy = -1;

            // Native ST7789 memory is 240x320; setRotation(1) maps to 320x240 landscape.
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            // This 3.2" panel needs INVOFF (MusicGoGoGo confirmed), otherwise colors invert.
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};
