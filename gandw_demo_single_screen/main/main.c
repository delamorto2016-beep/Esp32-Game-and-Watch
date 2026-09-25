#include <stdio.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include <gw_system.h>
#include <gw_romloader.h>

#define DISPLAY_HEIGHT		320
#define RENDER_HEIGHT		240
#define RENDER_PADDING		40

// ============================================================
//  КНОПКИ — свободные пины, не пересекаются с LCD и аудио
// ============================================================
#define BUTTON_GAME_A		GPIO_NUM_6
#define BUTTON_GAME_B		GPIO_NUM_7
#define BUTTON_TIME			GPIO_NUM_15
#define BUTTON_LEFT			GPIO_NUM_16
#define BUTTON_RIGHT		GPIO_NUM_17
#define BUTTON_ALARM		GPIO_NUM_18
#define BUTTON_ACL			GPIO_NUM_21

// ============================================================
//  LCD (ST7789 240x320) — проверенная рабочая конфигурация
// ============================================================
#define LCD_PIXEL_CLOCK_HZ	(40 * 1000 * 1000)
#define LCD_CMD_BITS		8
#define LCD_PARAM_BITS		8

#define LCD_HOST			SPI2_HOST
#define LCD_SCLK			5
#define LCD_MOSI			4
#define LCD_MISO			-1
#define LCD_DC				2
#define LCD_RST				3
#define LCD_CS				1

// ============================================================
//  АУДИО (I2S) — свободные пины
// ============================================================
#define AUD_I2S_BCK			38
#define AUD_I2S_WS			39
#define AUD_I2S_DATA		40


unsigned char *ROM_DATA;
unsigned int ROM_DATA_LENGTH;

unsigned int gw_get_buttons()
{
	uint32_t hw_buttons = 0;

	if (gpio_get_level(BUTTON_TIME) == 0) {
		hw_buttons |= GW_BUTTON_B + GW_BUTTON_TIME;
	}
	else if (gpio_get_level(BUTTON_RIGHT) == 0) {
		hw_buttons |= GW_BUTTON_RIGHT;
	}
	else if (gpio_get_level(BUTTON_LEFT) == 0) {
		hw_buttons |= GW_BUTTON_LEFT;
	}
	else if (gpio_get_level(BUTTON_GAME_A) == 0) {
		hw_buttons |= GW_BUTTON_GAME;
	}
	else if (gpio_get_level(BUTTON_GAME_B) == 0) {
		hw_buttons |= GW_BUTTON_TIME;
	}
	else if (gpio_get_level(BUTTON_ALARM) == 0) {
		hw_buttons |= GW_BUTTON_B + GW_BUTTON_GAME;
	}
	else if (gpio_get_level(BUTTON_ACL) == 0) {
		gw_system_reset();
	}

	return hw_buttons;
}

i2s_chan_handle_t setup_audio_i2s() {

	i2s_chan_handle_t i2s_audio_handle;
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_audio_handle, NULL));

	i2s_std_config_t i2s_config = {
		.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(GW_SYS_FREQ),
		.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
		.gpio_cfg = {
			.mclk = GPIO_NUM_NC,
			.bclk = AUD_I2S_BCK,
			.ws   = AUD_I2S_WS,
			.dout = AUD_I2S_DATA,
			.din  = GPIO_NUM_NC,
		}
	};

	ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_audio_handle, &i2s_config));
	ESP_ERROR_CHECK(i2s_channel_enable(i2s_audio_handle));

	return i2s_audio_handle;
}

// ============================================================
//  ИНИЦИАЛИЗАЦИЯ ST7789 240x320
// ============================================================
esp_lcd_panel_handle_t setup_lcd_spi() {

	esp_lcd_panel_handle_t spi_lcd_handle = NULL;
	esp_lcd_panel_io_handle_t io_handle = NULL;

	spi_bus_config_t buscfg = {
		.sclk_io_num = LCD_SCLK,
		.mosi_io_num = LCD_MOSI,
		.miso_io_num = LCD_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = GW_SCREEN_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t)
	};
	ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

	esp_lcd_panel_io_spi_config_t io_config = {
		.dc_gpio_num = LCD_DC,
		.cs_gpio_num = LCD_CS,
		.pclk_hz = LCD_PIXEL_CLOCK_HZ,
		.lcd_cmd_bits = LCD_CMD_BITS,
		.lcd_param_bits = LCD_PARAM_BITS,
		.spi_mode = 3,
		.trans_queue_depth = 10
	};
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

	esp_lcd_panel_dev_config_t panel_config = {
		.reset_gpio_num = LCD_RST,
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
		.bits_per_pixel = 16
	};

	ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &spi_lcd_handle));

	ESP_ERROR_CHECK(esp_lcd_panel_reset(spi_lcd_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_init(spi_lcd_handle));
	esp_lcd_panel_swap_xy(spi_lcd_handle, true);
	esp_lcd_panel_mirror(spi_lcd_handle, false, true);
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(spi_lcd_handle, true));
	esp_lcd_panel_invert_color(spi_lcd_handle, true);

	return spi_lcd_handle;
}

void setup_buttons() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_TIME)   |
                        (1ULL << BUTTON_GAME_A) |
                        (1ULL << BUTTON_GAME_B) |
                        (1ULL << BUTTON_ALARM)  |
                        (1ULL << BUTTON_ACL)    |
                        (1ULL << BUTTON_LEFT)   |
                        (1ULL << BUTTON_RIGHT),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);
}

void app_main(void)
{
	// Буфер под полный экран 240x320 (для очистки и отрисовки)
	uint16_t *framebuffer = (uint16_t *)heap_caps_malloc(
		GW_SCREEN_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
		MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

	uint16_t audio_buffer[GW_AUDIO_BUFFER_LENGTH];

	// звук
	i2s_chan_handle_t i2s_audio_handle = setup_audio_i2s();

	// экран
	esp_lcd_panel_handle_t spi_lcd_handle = setup_lcd_spi();

	// кнопки
	setup_buttons();

	// Полная очистка экрана 240x320 чёрным
	for (int i = 0; i < DISPLAY_HEIGHT * GW_SCREEN_WIDTH; i++) {
		framebuffer[i] = 0x0000;
	}
	esp_lcd_panel_draw_bitmap(spi_lcd_handle, 0, 0, GW_SCREEN_WIDTH, DISPLAY_HEIGHT, framebuffer);

	// загрузка ROM
	extern const uint8_t rom_gw_start[] asm("_binary_gnwparachute_gw_start");
	extern const uint8_t rom_gw_end[]   asm("_binary_gnwparachute_gw_end");

	ROM_DATA = (unsigned char *)rom_gw_start;
	ROM_DATA_LENGTH = rom_gw_end - rom_gw_start;

	gw_system_romload();

	gw_system_sound_init();
	gw_system_config();
	gw_system_start();
	gw_system_reset();

	uint16_t sample = 0;
	int display_update_count = 0;

	while (true) {

		display_update_count++;

		gw_system_run(GW_SYSTEM_CYCLES);

		// LCD — игра рисуется в полосе 240x240 с отступом 40 сверху
		if (display_update_count == 8) {

			gw_system_blit(framebuffer);

			esp_lcd_panel_draw_bitmap(spi_lcd_handle,
			                          0, RENDER_PADDING,
			                          GW_SCREEN_WIDTH,
			                          RENDER_HEIGHT + RENDER_PADDING,
			                          framebuffer);

			display_update_count = 0;
		}

		// AUDIO
		for (size_t i = 0; i < GW_AUDIO_BUFFER_LENGTH; i++)
		{
			sample = 0;

			if (gw_audio_buffer[i] > 0) {
				sample = 2000;
			}

			audio_buffer[i] = sample;
		}

		size_t bytes_written;
		i2s_channel_write(i2s_audio_handle, audio_buffer, sizeof(audio_buffer),
		                  &bytes_written, portMAX_DELAY);

		gw_audio_buffer_copied = true;
	}
}
