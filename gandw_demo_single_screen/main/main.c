#include <stdio.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"

// ============================================================
//  ТОЛЬКО ЭКРАН — ST7789 240x320
// ============================================================
#define LCD_HOST			SPI2_HOST
#define LCD_SCLK			5
#define LCD_MOSI			4
#define LCD_MISO			-1
#define LCD_DC				2
#define LCD_RST				3
#define LCD_CS				1

#define LCD_PIXEL_CLOCK_HZ	(10 * 1000 * 1000)
#define LCD_CMD_BITS		8
#define LCD_PARAM_BITS		8

#define DISPLAY_WIDTH		240
#define DISPLAY_HEIGHT		320

esp_lcd_panel_handle_t setup_lcd_spi(void)
{
	esp_lcd_panel_handle_t panel = NULL;
	esp_lcd_panel_io_handle_t io = NULL;

	spi_bus_config_t buscfg = {
		.sclk_io_num = LCD_SCLK,
		.mosi_io_num = LCD_MOSI,
		.miso_io_num = LCD_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 240 * 60 * sizeof(uint16_t)   // 28 КБ — безопасный чанк
	};
	ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

	esp_lcd_panel_io_spi_config_t io_config = {
		.dc_gpio_num = LCD_DC,
		.cs_gpio_num = LCD_CS,
		.pclk_hz = LCD_PIXEL_CLOCK_HZ,
		.lcd_cmd_bits = LCD_CMD_BITS,
		.lcd_param_bits = LCD_PARAM_BITS,
		.spi_mode = 0,
		.trans_queue_depth = 10
	};
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
		(esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io));

	esp_lcd_panel_dev_config_t panel_config = {
		.reset_gpio_num = LCD_RST,
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
		.bits_per_pixel = 16
	};
	ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));

	// ---- только базовые вызовы, без swap_xy / mirror / invert ----
	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

	return panel;
}

void app_main(void)
{
	printf("=== LCD MINIMAL TEST ===\n");

	esp_lcd_panel_handle_t lcd = setup_lcd_spi();
	printf("LCD init done\n");

	uint16_t *fb = (uint16_t *)heap_caps_malloc(
		DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
		MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

	if (fb == NULL) {
		printf("FATAL: framebuffer malloc failed\n");
		return;
	}
	printf("Framebuffer OK at %p\n", fb);

	// Сплошной красный
	for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
		fb[i] = 0xF800;
	}

	// Рисуем полосами по 60 строк, чтобы не упираться в лимит DMA
	for (int y = 0; y < DISPLAY_HEIGHT; y += 60) {
		int h = (DISPLAY_HEIGHT - y < 60) ? (DISPLAY_HEIGHT - y) : 60;
		esp_lcd_panel_draw_bitmap(lcd,
		                          0, y,
		                          DISPLAY_WIDTH, y + h,
		                          fb + (y * DISPLAY_WIDTH));
		printf("drawn rows %d..%d\n", y, y + h);
	}

	printf("=== DONE — should be RED ===\n");

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
