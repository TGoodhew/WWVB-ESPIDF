/*
 * WWVB Unit Tests - Main Entry Point
 * 
 * This file runs all unit tests for WWVB encoder and DST calculation modules.
 * Uses ESP-IDF Unity test framework.
 * 
 * To run tests:
 *   cd test
 *   idf.py build
 *   idf.py -p /dev/ttyUSB0 flash monitor
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"

static const char *TAG = "WWVB_TEST";

// Test runners from individual test files
extern void run_wwvb_encoder_tests(void);
extern void run_dst_calc_tests(void);

void app_main(void)
{
    ESP_LOGI(TAG, "Starting WWVB Unit Tests");
    ESP_LOGI(TAG, "====================================");
    
    UNITY_BEGIN();
    
    // Run all test suites
    run_wwvb_encoder_tests();
    run_dst_calc_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "WWVB Unit Tests Complete");
}
