#include <Arduino.h>
#include <unity.h>
#include <vfs_api.h>
#include <LoRa.h>

#include "nuggetConf.h"
#include "src/nuggetLora/nuggetLora.h"
#include "src/RubberNugget.h"

/**
 * Unit test nuggetLora component
 * 
 * ALL TESTS ARE HARDWARE-ONLY, DID NOT HAVE TIME TO BUILD MOCKING FRAMEWORK
*/
String STR_TO_TEST = "TEST STR";
NuggetConfig default_conf = {
        .locale = "EN",
        .network = "Nugget AP",
        .password = "nugget123",
        .pid = 0x20b,
        .vid = 0x05ac,
        .lora_enabled = false,
        .lora_module_type = RFM95,
        .lora_addr = 0x01,
        .lora_RFM_syncword = 0xF3,
    };
// NuggetLora nugLora(default_conf);

NuggetLora nugLora(default_conf);

void setUp(void) {
    // set stuff up here


    // nugLora = NuggetLora(default_conf);
    // NuggetLora nugLora(default_conf);
}

void tearDown(void) {
    // clean stuff up here
}


/**
 * Tests functionality that tests the SPI bus for presence
 * of RFM95 module
 * MUST BE RUN ON HARDWARE, MOCK NOT PRESENT
*/
void test_detectRFMModule(void) {
    TEST_ASSERT_TRUE_MESSAGE(nugLora.initModule(), "Failed to detect RFM95 on SPI bus");

}


void test_string_substring(void) {
    TEST_ASSERT_EQUAL_STRING("Hello", STR_TO_TEST.substring(0, 5).c_str());
}


void setup()
{
    Serial.begin(115200);

    delay(2000); // service delay
    UNITY_BEGIN();

    RUN_TEST(test_detectRFMModule);
    // RUN_TEST(test_string_substring);

    UNITY_END(); // stop unit testing
}

void loop()
{
}