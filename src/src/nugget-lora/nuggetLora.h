#ifndef NUGGET_LORA_H
#define NUGGET_LORA_H

/**
 * Public interface for Nugget-Nugget Lora communication

 * @author 0xjmux
 * @date April 2024
 * 
 * More information about LoRa on the Nugget can be found here: 
 * https://github.com/RetiaLLC/RFM95w_LoRa_Nugget
 * 
*/

#include "SPI.h"
#include "LoRa.h"



// Nugget LoRa addon pins
// SPI pins. RFM95 is MSB first
#define LORA_SCK 15
#define LORA_MISO 17
#define LORA_MOSI 21
#define LORA_SS 34
#define LORA_RST 5
#define LORA_DIO0 16


// NUGGET LORA COMMUNICTION CONFIGURATION
#define LORA_SYNCWORD 0xF3

/* communication modes for nugget-nugget communication 
 * NUG_COMM_MODE_BASIC - basic controls using buttons, like HID injection
 * NUG_COMM_MODE_CMD - command modes, using yet-to-be-determined syntax
 * RESERVED - reserved for future use
 */
typedef enum nug_comm_modes {
    NUG_COMM_MODE_BASIC,
    NUG_COMM_MODE_CMD,
    NUG_WEB_LORA_MESSAGER,
    NUG_COMM_RESERVED1,
    NUG_COMM_RESERVED2
} nug_comm_modes;

// standardized set of basic commands that can be used to control another nugget over lora. These will be expanded over time.
#define NUM_BASIC_LORA_CMDS 11
enum nugget_lora_cmds {BTN_UP, BTN_DN, BTN_L, BTN_R, LED_ON, LED_OFF, LED_COLOR, SW_RST, OLED_ON, OLED_OFF, OLED_DISPLAY_TEXT};
// most of these are simple binary commands - some will need to take arguments to be useful
// LED_COLOR - will need 3x uint8_t
// OLED_DISPLAY_TEXT - line {1..4}, const char* text

class NuggetLora {
  public: 
    NuggetLora(void);
    void initSPI(void);
    bool lora_recv_cb();

    bool testForRFM(void);

   private: 
    // uint8_t singleTransfer(uint8_t address, uint8_t value);
    uint8_t readLoraReg(uint8_t address);
    LoRaClass lora;
    SPIClass* spi;

};




#endif