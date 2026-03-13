#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

// Redirect selected runtime dependencies to deterministic test doubles.
#define app_main wwvb_app_main_disabled_for_tests
#define time test_time
#define esp_timer_get_time test_esp_timer_get_time
#define esp_timer_start_once test_esp_timer_start_once
#define esp_timer_start_periodic test_esp_timer_start_periodic
#define esp_timer_is_active test_esp_timer_is_active
#define gpio_set_level test_gpio_set_level
#define ledc_set_duty test_ledc_set_duty
#define ledc_update_duty test_ledc_update_duty
#undef xTaskNotifyGive
#define xTaskNotifyGive(xTaskToNotify) test_xTaskNotifyGive(xTaskToNotify)
#define esp_wifi_connect test_esp_wifi_connect
#define xEventGroupSetBits test_xEventGroupSetBits
#define esp_wifi_get_mac test_esp_wifi_get_mac

// Compile production source into this translation unit so static symbols
// can be directly tested here.
#include "../../main/main.c"

#undef time
#undef esp_timer_get_time
#undef esp_timer_start_once
#undef esp_timer_start_periodic
#undef esp_timer_is_active
#undef gpio_set_level
#undef ledc_set_duty
#undef ledc_update_duty
#undef xTaskNotifyGive
#undef esp_wifi_connect
#undef xEventGroupSetBits
#undef esp_wifi_get_mac

typedef struct
{
	time_t fake_now;
	int time_calls;
	int64_t fake_now_us;
	int esp_timer_get_time_calls;

	esp_timer_handle_t start_once_handle;
	uint64_t start_once_period_us;
	int start_once_calls;

	esp_timer_handle_t start_periodic_handle;
	uint64_t start_periodic_period_us;
	int start_periodic_calls;

	esp_timer_handle_t active_timer_handle;

	gpio_num_t gpio_last_num;
	uint32_t gpio_last_level;
	int gpio_set_calls;

	uint32_t ledc_last_duty;
	int ledc_set_duty_calls;
	int ledc_update_calls;

	TaskHandle_t last_notified_task;
	int notify_calls;

	int wifi_connect_calls;

	EventBits_t event_bits_or;
	int event_group_set_calls;

	uint8_t fake_mac[6];
	int wifi_get_mac_calls;
} test_fakes_t;

static test_fakes_t g_fakes;

static void reset_test_fakes(void)
{
	memset(&g_fakes, 0, sizeof(g_fakes));
	memset(WWVBBufferA, 0, sizeof(WWVBBufferA));
	memset(WWVBBufferB, 0, sizeof(WWVBBufferB));
	activeWWVBBuffer = WWVBBufferA;
	stagingWWVBBuffer = WWVBBufferB;
	slot = 0;
	stagingFrameReady = false;
	frameBuilderTaskHandle = NULL;
	s_retry_num = 0;
	provisioning_in_progress = false;
	wifi_state = WIFI_STATE_DISCONNECTED;
	memset(&runtime_metrics, 0, sizeof(runtime_metrics));

	TimerBit0 = (esp_timer_handle_t)0x10;
	TimerBit1 = (esp_timer_handle_t)0x11;
	TimerBitMarker = (esp_timer_handle_t)0x12;
	TimerSecond = (esp_timer_handle_t)0x13;
	TimerMinuteAlign = (esp_timer_handle_t)0x14;
	TimerSyncWait = (esp_timer_handle_t)0x15;
#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
	TimerHeartbeat = (esp_timer_handle_t)0x16;
#endif

	ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
	ledc_channel.channel = LEDC_CHANNEL_0;
}

time_t test_time(time_t *tloc)
{
	g_fakes.time_calls++;
	if (tloc != NULL)
	{
		*tloc = g_fakes.fake_now;
	}
	return g_fakes.fake_now;
}

int64_t test_esp_timer_get_time(void)
{
	g_fakes.esp_timer_get_time_calls++;
	return g_fakes.fake_now_us;
}

esp_err_t test_esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
	g_fakes.start_once_calls++;
	g_fakes.start_once_handle = timer;
	g_fakes.start_once_period_us = timeout_us;
	return ESP_OK;
}

esp_err_t test_esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
	g_fakes.start_periodic_calls++;
	g_fakes.start_periodic_handle = timer;
	g_fakes.start_periodic_period_us = period;
	return ESP_OK;
}

bool test_esp_timer_is_active(esp_timer_handle_t timer)
{
	return (timer == g_fakes.active_timer_handle);
}

esp_err_t test_gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
	g_fakes.gpio_set_calls++;
	g_fakes.gpio_last_num = gpio_num;
	g_fakes.gpio_last_level = level;
	return ESP_OK;
}

esp_err_t test_ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
	(void)speed_mode;
	(void)channel;
	g_fakes.ledc_set_duty_calls++;
	g_fakes.ledc_last_duty = duty;
	return ESP_OK;
}

esp_err_t test_ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
	(void)speed_mode;
	(void)channel;
	g_fakes.ledc_update_calls++;
	return ESP_OK;
}

BaseType_t test_xTaskNotifyGive(TaskHandle_t xTaskToNotify)
{
	g_fakes.notify_calls++;
	g_fakes.last_notified_task = xTaskToNotify;
	return pdTRUE;
}

esp_err_t test_esp_wifi_connect(void)
{
	g_fakes.wifi_connect_calls++;
	return ESP_OK;
}

EventBits_t test_xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
	(void)xEventGroup;
	g_fakes.event_group_set_calls++;
	g_fakes.event_bits_or |= uxBitsToSet;
	return g_fakes.event_bits_or;
}

esp_err_t test_esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
	(void)ifx;
	g_fakes.wifi_get_mac_calls++;
	memcpy(mac, g_fakes.fake_mac, 6);
	return ESP_OK;
}

static int decode_minute(const uint8_t *signal)
{
	int bits = (signal[1] << 6) | (signal[2] << 5) | (signal[3] << 4) | (signal[5] << 3) |
			   (signal[6] << 2) | (signal[7] << 1) | signal[8];
	int tens = (bits >> 4) & 0xF;
	int ones = bits & 0xF;
	return (tens * 10) + ones;
}

static int decode_hour(const uint8_t *signal)
{
	int bits = (signal[12] << 5) | (signal[13] << 4) | (signal[15] << 3) |
			   (signal[16] << 2) | (signal[17] << 1) | signal[18];
	int tens = (bits >> 4) & 0xF;
	int ones = bits & 0xF;
	return (tens * 10) + ones;
}

static int decode_day_of_year(const uint8_t *signal)
{
	int bits = (signal[22] << 9) | (signal[23] << 8) | (signal[25] << 7) | (signal[26] << 6) |
			   (signal[27] << 5) | (signal[28] << 4) | (signal[30] << 3) | (signal[31] << 2) |
			   (signal[32] << 1) | signal[33];
	int hundreds = (bits >> 8) & 0xF;
	int tens = (bits >> 4) & 0xF;
	int ones = bits & 0xF;
	return (hundreds * 100) + (tens * 10) + ones;
}

static int decode_year_2digit(const uint8_t *signal)
{
	int bits = (signal[45] << 7) | (signal[46] << 6) | (signal[47] << 5) | (signal[48] << 4) |
			   (signal[50] << 3) | (signal[51] << 2) | (signal[52] << 1) | signal[53];
	int tens = (bits >> 4) & 0xF;
	int ones = bits & 0xF;
	return (tens * 10) + ones;
}

TEST_CASE("SetupWWVBArray encodes next minute across year rollover", "[main][frame]")
{
	reset_test_fakes();
	uint8_t signal[60] = {0};

	g_fakes.fake_now = (time_t)1767225570; // 2025-12-31 23:59:30 UTC
	SetupWWVBArray(signal);

	TEST_ASSERT_EQUAL_INT(1, g_fakes.time_calls);
	TEST_ASSERT_EQUAL_INT(0, decode_minute(signal));
	TEST_ASSERT_EQUAL_INT(0, decode_hour(signal));
	TEST_ASSERT_EQUAL_INT(1, decode_day_of_year(signal));
	TEST_ASSERT_EQUAL_INT(26, decode_year_2digit(signal));
	TEST_ASSERT_EQUAL_UINT8(0, signal[57]);
	TEST_ASSERT_EQUAL_UINT8(0, signal[58]);
}

TEST_CASE("SetupWWVBArray encodes leap-day rollover correctly", "[main][frame]")
{
	reset_test_fakes();
	uint8_t signal[60] = {0};

	g_fakes.fake_now = (time_t)1709164770; // 2024-02-28 23:59:30 UTC
	SetupWWVBArray(signal);

	TEST_ASSERT_EQUAL_INT(0, decode_minute(signal));
	TEST_ASSERT_EQUAL_INT(0, decode_hour(signal));
	TEST_ASSERT_EQUAL_INT(60, decode_day_of_year(signal));
	TEST_ASSERT_EQUAL_INT(24, decode_year_2digit(signal));
	TEST_ASSERT_EQUAL_UINT8(1, signal[55]);
}

TEST_CASE("SetupWWVBArray encodes non-leap Feb rollover correctly", "[main][frame]")
{
	reset_test_fakes();
	uint8_t signal[60] = {0};

	g_fakes.fake_now = (time_t)1740787170; // 2025-02-28 23:59:30 UTC
	SetupWWVBArray(signal);

	TEST_ASSERT_EQUAL_INT(0, decode_minute(signal));
	TEST_ASSERT_EQUAL_INT(0, decode_hour(signal));
	TEST_ASSERT_EQUAL_INT(60, decode_day_of_year(signal));
	TEST_ASSERT_EQUAL_INT(25, decode_year_2digit(signal));
	TEST_ASSERT_EQUAL_UINT8(0, signal[55]);
}

TEST_CASE("TimerSecond_ISR schedules 200ms timer for symbol 0", "[main][isr]")
{
	reset_test_fakes();
	g_fakes.fake_now_us = 1000000;
	slot = 5;
	activeWWVBBuffer[5] = 0;

	TimerSecond_ISR(NULL);

	TEST_ASSERT_EQUAL_INT(1, g_fakes.start_once_calls);
	TEST_ASSERT_EQUAL_PTR(TimerBit0, g_fakes.start_once_handle);
	TEST_ASSERT_EQUAL_UINT32(200000UL, (uint32_t)g_fakes.start_once_period_us);
	TEST_ASSERT_EQUAL_UINT32(0, g_fakes.ledc_last_duty);
}

TEST_CASE("TimerSecond_ISR schedules 500ms timer for symbol 1", "[main][isr]")
{
	reset_test_fakes();
	g_fakes.fake_now_us = 2000000;
	slot = 8;
	activeWWVBBuffer[8] = 1;

	TimerSecond_ISR(NULL);

	TEST_ASSERT_EQUAL_INT(1, g_fakes.start_once_calls);
	TEST_ASSERT_EQUAL_PTR(TimerBit1, g_fakes.start_once_handle);
	TEST_ASSERT_EQUAL_UINT32(500000UL, (uint32_t)g_fakes.start_once_period_us);
	TEST_ASSERT_EQUAL_UINT32(0, g_fakes.ledc_last_duty);
}

TEST_CASE("TimerSecond_ISR schedules 800ms timer for marker", "[main][isr]")
{
	reset_test_fakes();
	g_fakes.fake_now_us = 3000000;
	slot = 9;
	activeWWVBBuffer[9] = 2;

	TimerSecond_ISR(NULL);

	TEST_ASSERT_EQUAL_INT(1, g_fakes.start_once_calls);
	TEST_ASSERT_EQUAL_PTR(TimerBitMarker, g_fakes.start_once_handle);
	TEST_ASSERT_EQUAL_UINT32(800000UL, (uint32_t)g_fakes.start_once_period_us);
	TEST_ASSERT_EQUAL_UINT32(0, g_fakes.ledc_last_duty);
}

TEST_CASE("TimerSecond_ISR swaps buffers only at minute boundary", "[main][isr]")
{
	reset_test_fakes();
	g_fakes.fake_now_us = 61000000;

	uint8_t *old_active = activeWWVBBuffer;
	uint8_t *old_staging = stagingWWVBBuffer;
	stagingFrameReady = true;
	slot = 0;
	/* frameBuilderTaskHandle stays NULL so xTaskNotifyGive is not called */
	activeWWVBBuffer[0] = 0;

	TimerSecond_ISR(NULL);

	TEST_ASSERT_EQUAL_PTR(old_staging, activeWWVBBuffer);
	TEST_ASSERT_EQUAL_PTR(old_active, stagingWWVBBuffer);
	TEST_ASSERT_FALSE(stagingFrameReady);
	TEST_ASSERT_EQUAL_UINT32(1, runtime_metrics.frames_swapped);
}

TEST_CASE("TimerSecond_ISR does not swap buffers away from slot zero", "[main][isr]")
{
	reset_test_fakes();
	g_fakes.fake_now_us = 71000000;

	uint8_t *old_active = activeWWVBBuffer;
	uint8_t *old_staging = stagingWWVBBuffer;
	stagingFrameReady = true;
	slot = 1;
	activeWWVBBuffer[1] = 0;

	TimerSecond_ISR(NULL);

	TEST_ASSERT_EQUAL_PTR(old_active, activeWWVBBuffer);
	TEST_ASSERT_EQUAL_PTR(old_staging, stagingWWVBBuffer);
	TEST_ASSERT_TRUE(stagingFrameReady);
	TEST_ASSERT_EQUAL_UINT32(0, runtime_metrics.frames_swapped);
}

TEST_CASE("TimerSecond_ISR wraps slot and triggers health path at end of frame", "[main][isr]")
{
	reset_test_fakes();
	g_fakes.fake_now_us = 5000000;
	slot = 59;
	activeWWVBBuffer[59] = 2;

	TimerSecond_ISR(NULL);

	TEST_ASSERT_EQUAL_UINT8(0, slot);
	TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)runtime_metrics.uptime_sec);
}

TEST_CASE("calculateDSTDays supports multiple years", "[main][dst]")
{
	int start = 0;
	int end = 0;

	calculateDSTDays(2023, &start, &end);
	TEST_ASSERT_EQUAL_INT(71, start);
	TEST_ASSERT_EQUAL_INT(309, end);

	calculateDSTDays(2024, &start, &end);
	TEST_ASSERT_EQUAL_INT(70, start);
	TEST_ASSERT_EQUAL_INT(308, end);

	calculateDSTDays(2026, &start, &end);
	TEST_ASSERT_EQUAL_INT(67, start);
	TEST_ASSERT_EQUAL_INT(305, end);
}

TEST_CASE("isDaylightSavingTime matches start and end boundaries", "[main][dst]")
{
	TEST_ASSERT_FALSE(isDaylightSavingTime(2024, 69));
	TEST_ASSERT_TRUE(isDaylightSavingTime(2024, 70));
	TEST_ASSERT_TRUE(isDaylightSavingTime(2024, 307));
	TEST_ASSERT_FALSE(isDaylightSavingTime(2024, 308));

	TEST_ASSERT_FALSE(isDaylightSavingTime(2026, 66));
	TEST_ASSERT_TRUE(isDaylightSavingTime(2026, 67));
	TEST_ASSERT_TRUE(isDaylightSavingTime(2026, 304));
	TEST_ASSERT_FALSE(isDaylightSavingTime(2026, 305));
}

TEST_CASE("get_device_service_name formats PROV suffix from MAC", "[main][wifi]")
{
	reset_test_fakes();
	char service_name[12] = {0};
	g_fakes.fake_mac[0] = 0xAA;
	g_fakes.fake_mac[1] = 0xBB;
	g_fakes.fake_mac[2] = 0xCC;
	g_fakes.fake_mac[3] = 0xA1;
	g_fakes.fake_mac[4] = 0xB2;
	g_fakes.fake_mac[5] = 0xC3;

	get_device_service_name(service_name, sizeof(service_name));

	TEST_ASSERT_EQUAL_INT(1, g_fakes.wifi_get_mac_calls);
	TEST_ASSERT_EQUAL_STRING("PROV_A1B2C3", service_name);
}

TEST_CASE("wifi_event_handler connects on STA_START only when not provisioning", "[main][wifi]")
{
	reset_test_fakes();

	provisioning_in_progress = false;
	wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
	TEST_ASSERT_EQUAL_INT(1, g_fakes.wifi_connect_calls);
	TEST_ASSERT_EQUAL_INT(WIFI_STATE_CONNECTING, wifi_state);

	reset_test_fakes();
	provisioning_in_progress = true;
	wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
	TEST_ASSERT_EQUAL_INT(0, g_fakes.wifi_connect_calls);
}

TEST_CASE("wifi_event_handler retries and then sets fail bit", "[main][wifi]")
{
	reset_test_fakes();

	s_retry_num = 9;
	wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
	TEST_ASSERT_EQUAL_INT(10, s_retry_num);
	TEST_ASSERT_EQUAL_INT(1, g_fakes.wifi_connect_calls);
	TEST_ASSERT_EQUAL_UINT32(1, runtime_metrics.wifi_disconnects);

	wifi_event_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
	TEST_ASSERT_EQUAL_INT(10, s_retry_num);
	TEST_ASSERT_EQUAL_INT(1, g_fakes.wifi_connect_calls);
	TEST_ASSERT_NOT_EQUAL(0, g_fakes.event_bits_or & WIFI_FAIL_BIT);
	TEST_ASSERT_EQUAL_UINT32(2, runtime_metrics.wifi_disconnects);
}

TEST_CASE("wifi_event_handler GOT_IP updates state and counters", "[main][wifi]")
{
	reset_test_fakes();
	ip_event_got_ip_t got_ip = {0};

	wifi_state = WIFI_STATE_DISCONNECTED;
	s_retry_num = 5;
	wifi_event_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip);

	TEST_ASSERT_EQUAL_INT(WIFI_STATE_CONNECTED, wifi_state);
	TEST_ASSERT_EQUAL_INT(0, s_retry_num);
	TEST_ASSERT_EQUAL_UINT32(1, runtime_metrics.wifi_reconnects);
	TEST_ASSERT_NOT_EQUAL(0, g_fakes.event_bits_or & WIFI_CONNECTED_BIT);

	wifi_event_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip);
	TEST_ASSERT_EQUAL_UINT32(1, runtime_metrics.wifi_reconnects);
}

