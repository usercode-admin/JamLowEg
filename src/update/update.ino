#include <Arduino.h>
#include <SPI.h>
#include "esp_attr.h"
#include "soc/spi_reg.h"
#include "soc/gpio_struct.h"

#define PIN_SCK       6
#define PIN_MISO      2     
#define PIN_MOSI      7
#define PIN_CE        3
#define PIN_CSN       10

#define SPI_HZ        12000000UL

#define SWEEP_FULL    1

#define YIELD_EVERY   4096
#define TASK_PRIO     20

#define CSN_MASK      (1UL << PIN_CSN)
#define CE_MASK       (1UL << PIN_CE)

#define CSN_LOW()     (GPIO.out_w1tc.val = CSN_MASK)
#define CSN_HIGH()    (GPIO.out_w1ts.val = CSN_MASK)
#define CE_HIGH()     (GPIO.out_w1ts.val = CE_MASK)
#define CE_LOW()      (GPIO.out_w1tc.val = CE_MASK)

#define SPI2_CMD_REG    (*(volatile uint32_t *)(DR_REG_SPI2_BASE + 0x00))
#define SPI2_DATA_BUF0  (*(volatile uint32_t *)(DR_REG_SPI2_BASE + 0x58))
#define SPI2_MS_DLEN    (*(volatile uint32_t *)(DR_REG_SPI2_BASE + 0x1C))
#define SPI_USR_BIT     (1UL << 24)

IRAM_ATTR static inline __attribute__((always_inline))
void nrf_set_ch(uint8_t ch) {
    CSN_LOW();
    SPI2_DATA_BUF0 = ((uint32_t)ch << 8) | 0x25U;
    SPI2_CMD_REG   = SPI_USR_BIT;
    while (SPI2_CMD_REG & SPI_USR_BIT) {}
    CSN_HIGH();
}

IRAM_ATTR static inline void spi_put(uint8_t out) {
    SPI2_DATA_BUF0 = out;
    SPI2_CMD_REG   = SPI_USR_BIT;
    while (SPI2_CMD_REG & SPI_USR_BIT) {}
}

IRAM_ATTR static inline uint8_t spi_get(uint8_t out) {
    SPI2_DATA_BUF0 = out;
    SPI2_CMD_REG   = SPI_USR_BIT;
    while (SPI2_CMD_REG & SPI_USR_BIT) {}
    return (uint8_t)SPI2_DATA_BUF0;
}

IRAM_ATTR static inline void nrf_w(uint8_t reg, uint8_t val) {
    CSN_LOW();
    spi_put(0x20 | (reg & 0x1F));
    spi_put(val);
    CSN_HIGH();
}

IRAM_ATTR static inline uint8_t nrf_r(uint8_t reg) {
    uint8_t v;
    CSN_LOW();
    spi_put(reg & 0x1F);
    v = spi_get(0xFF);
    CSN_HIGH();
    return v;
}

static bool nrf_enter_cw(void) {
    uint8_t s = nrf_r(0x07);
    if (s == 0x00 || s == 0xFF) return false;

    nrf_w(0x00, 0x02);
    esp_rom_delay_us(2000);

    nrf_w(0x01, 0x00); 
    nrf_w(0x04, 0x00); 
    nrf_w(0x02, 0x00);
    nrf_w(0x1C, 0x00);
    nrf_w(0x1D, 0x00);

    nrf_w(0x06, 0x10 | 0x06);
    esp_rom_delay_us(15);

    nrf_w(0x06, 0x10 | 0x80 | 0x06);

    nrf_w(0x05, 0x00);

    CE_HIGH();

    return (nrf_r(0x06) & 0x90) == 0x90;
}

static void IRAM_ATTR __attribute__((optimize("O3"))) jam_task(void *arg) {
    if (!nrf_enter_cw()) {
        vTaskDelete(NULL);
    }

    SPI2_MS_DLEN = 15;

    uint32_t n = 0;

    for (;;) {
#if SWEEP_FULL
        for (uint8_t i = 0; i < 120; i += 8) {
            nrf_set_ch(i + 0);
            nrf_set_ch(i + 1);
            nrf_set_ch(i + 2);
            nrf_set_ch(i + 3);
            nrf_set_ch(i + 4);
            nrf_set_ch(i + 5);
            nrf_set_ch(i + 6);
            nrf_set_ch(i + 7);
        }
        nrf_set_ch(120);
        nrf_set_ch(121);
        nrf_set_ch(122);
        nrf_set_ch(123);
        nrf_set_ch(124);
#else
        for (uint8_t i = 0; i < 72; i += 8) {
            nrf_set_ch(i + 0);
            nrf_set_ch(i + 1);
            nrf_set_ch(i + 2);
            nrf_set_ch(i + 3);
            nrf_set_ch(i + 4);
            nrf_set_ch(i + 5);
            nrf_set_ch(i + 6);
            nrf_set_ch(i + 7);
        }
        // tail 72..78
        nrf_set_ch(72);
        nrf_set_ch(73);
        nrf_set_ch(74);
        nrf_set_ch(75);
        nrf_set_ch(76);
        nrf_set_ch(77);
        nrf_set_ch(78);
#endif

        if ((++n & (YIELD_EVERY - 1)) == 0) {
            vTaskDelay(1); 
        }
    }
}

void setup() {
    pinMode(PIN_CE, OUTPUT);
    pinMode(PIN_CSN, OUTPUT);
    CE_LOW();
    CSN_HIGH();

    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
    SPI.setFrequency(SPI_HZ);
    SPI2_MS_DLEN = 7;

    delay(50);

    xTaskCreatePinnedToCore(jam_task, "jam", 2048, NULL, TASK_PRIO, NULL, 0);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
