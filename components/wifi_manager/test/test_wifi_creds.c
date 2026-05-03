/**
 * @file test_wifi_creds.c
 * @brief Unit tests for WiFi credential storage (NVS)
 * 
 * Tests verify:
 * - Save/load roundtrip
 * - Validation (SSID <= 32, password <= 64)
 * - is_configured flag behavior
 * - Clear functionality
 */

#include <string.h>
#include <unity.h>
#include "wifi_manager.h"

// Test constants
#define TEST_SSID     "TestNetwork"
#define TEST_PASSWORD "SecretPassword123"
#define LONG_SSID     "ThisSSIDNameIsExactlyThirtyTwoCharactersLong!"  // 42 chars - invalid
#define LONG_PASSWORD "ThisPasswordIsWayTooLongAndShouldBeRejectedBecauseItExceedsTheSixtyFourCharacterLimitByQuiteABitActually"  // 115 chars - invalid

/**
 * Test: Fresh start - credentials not configured
 */
TEST_CASE("wifi_creds_is_configured returns false on fresh start", "[wifi_creds]")
{
    TEST_ASSERT_FALSE(wifi_creds_is_configured());
}

/**
 * Test: Save valid credentials and verify is_configured
 */
TEST_CASE("wifi_creds_save with valid credentials sets configured flag", "[wifi_creds]")
{
    esp_err_t err = wifi_creds_save(TEST_SSID, TEST_PASSWORD);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(wifi_creds_is_configured());
}

/**
 * Test: Load credentials after save (roundtrip)
 */
TEST_CASE("wifi_creds_load returns saved credentials", "[wifi_creds]")
{
    char ssid[32] = {0};
    char pass[64] = {0};
    
    // Save first
    wifi_creds_save(TEST_SSID, TEST_PASSWORD);
    
    // Load and verify
    esp_err_t err = wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING(TEST_SSID, ssid);
    TEST_ASSERT_EQUAL_STRING(TEST_PASSWORD, pass);
}

/**
 * Test: SSID length validation (> 32 chars rejected)
 */
TEST_CASE("wifi_creds_save rejects SSID longer than 32 characters", "[wifi_creds]")
{
    esp_err_t err = wifi_creds_save(LONG_SSID, TEST_PASSWORD);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_FALSE(wifi_creds_is_configured());
}

/**
 * Test: Password length validation (> 64 chars rejected)
 */
TEST_CASE("wifi_creds_save rejects password longer than 64 characters", "[wifi_creds]")
{
    esp_err_t err = wifi_creds_save(TEST_SSID, LONG_PASSWORD);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_FALSE(wifi_creds_is_configured());
}

/**
 * Test: Clear credentials resets configured flag
 */
TEST_CASE("wifi_creds_clear resets all fields and configured flag", "[wifi_creds]")
{
    // Setup: save credentials
    wifi_creds_save(TEST_SSID, TEST_PASSWORD);
    TEST_ASSERT_TRUE(wifi_creds_is_configured());
    
    // Clear
    esp_err_t err = wifi_creds_clear();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(wifi_creds_is_configured());
    
    // Verify load fails or returns empty
    char ssid[32] = {0};
    char pass[64] = {0};
    err = wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);  // Should fail as not configured
}

/**
 * Test: Empty SSID rejected
 */
TEST_CASE("wifi_creds_save rejects empty SSID", "[wifi_creds]")
{
    esp_err_t err = wifi_creds_save("", TEST_PASSWORD);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/**
 * Test: NULL parameters rejected
 */
TEST_CASE("wifi_creds_save rejects NULL parameters", "[wifi_creds]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_save(NULL, TEST_PASSWORD));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_save(TEST_SSID, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_save(NULL, NULL));
}
