#include <Arduino.h>
#include <unity.h>

/**
 * Unit test nuggetLora component
*/

String STR_TO_TEST;

void setUp(void) {
    // set stuff up here
    STR_TO_TEST = "Hello, world!";
}

void tearDown(void) {
    // clean stuff up here
    STR_TO_TEST = "";
}


/**
 * Tests functionality that tests the SPI bus for presence
 * of RFM95 module
 * MUST BE RUN ON HARDWARE, MOCK NOT PRESENT
*/
void test_RFM_SPI_bus_read(void) {
    TEST_ASSERT_TRUE_MESSAGE(testForRFM(), "Failed to detect RFM95 on SPI bus");

}

void test_string_concat(void) {
    String hello = "Hello, ";
    String world = "world!";
    TEST_ASSERT_EQUAL_STRING(STR_TO_TEST.c_str(), (hello + world).c_str());
}

void test_string_substring(void) {
    TEST_ASSERT_EQUAL_STRING("Hello", STR_TO_TEST.substring(0, 5).c_str());
}


void setup()
{
    delay(2000); // service delay
    UNITY_BEGIN();

    RUN_TEST(test_string_concat);
    RUN_TEST(test_string_substring);

    UNITY_END(); // stop unit testing
}

void loop()
{
}