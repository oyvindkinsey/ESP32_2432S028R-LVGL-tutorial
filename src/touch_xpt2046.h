#pragma once

#include <stdint.h>
#include "driver/spi_master.h"

class Xpt2046 {
public:
    Xpt2046(spi_host_device_t spi_host, int cs_pin, int irq_pin);
    bool init();
    bool read_raw(uint16_t *x, uint16_t *y, uint16_t *z);
    bool is_pressed();

private:
    spi_host_device_t _spi_host;
    int _cs_pin;
    int _irq_pin;
    spi_device_handle_t _spi;
};
