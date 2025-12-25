#include "touch_xpt2046.h"
#include "driver/gpio.h"
#include <cstring>
#include "esp_log.h"

#define TAG "XPT2046"

Xpt2046::Xpt2046(spi_host_device_t spi_host, int cs_pin, int irq_pin)
    : _spi_host(spi_host), _cs_pin(cs_pin), _irq_pin(irq_pin), _spi(nullptr) {}

bool Xpt2046::init() {
    gpio_config_t shim_gpio_config = {
        .pin_bit_mask = (1ULL << _irq_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // External pull-up present, internal not avail on GPIO36
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&shim_gpio_config);

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 2500000;           // 2.5 MHz
    devcfg.mode = 0;                           // SPI mode 0
    devcfg.spics_io_num = _cs_pin;
    devcfg.queue_size = 1;
    
    esp_err_t ret = spi_bus_add_device(_spi_host, &devcfg, &_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device");
        return false;
    }
    return true;
}

bool Xpt2046::is_pressed() {
    return gpio_get_level((gpio_num_t)_irq_pin) == 0;
}

bool Xpt2046::read_raw(uint16_t *x, uint16_t *y, uint16_t *z) {
    // Debug: Print IRQ state
    // int irq = gpio_get_level((gpio_num_t)_irq_pin);
    
    if (!is_pressed()) return false;

    printf("XPT: IRQ Low (pressed), Reading...\n");

    // Use Z1 pressure as secondary validation
    uint16_t z1 = 0;
    spi_transaction_t t = {};
    t.length = 24;
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    
    t.tx_data[0] = 0xB0; // Z1
    if (spi_device_transmit(_spi, &t) != ESP_OK) return false;
    z1 = ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;

    printf("Raw Z1: %d\n", z1);

    if (z1 < 10) {
        printf("Z1 too low, ignoring\n");
        return false; 
    }

    const uint8_t CMD_X = 0xD0; 
    const uint8_t CMD_Y = 0x90; 

    // Read X
    t.tx_data[0] = CMD_X;
    if (spi_device_transmit(_spi, &t) != ESP_OK) return false;
    uint16_t touch_x = ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;

    // Read Y
    t.tx_data[0] = CMD_Y;
    if (spi_device_transmit(_spi, &t) != ESP_OK) return false;
    uint16_t touch_y = ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;

    *x = touch_x;
    *y = touch_y;
    *z = z1; 

    return true;
}
