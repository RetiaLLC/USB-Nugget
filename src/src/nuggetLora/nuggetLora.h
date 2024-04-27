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

#include <SPI.h>
#include "LoRa.h"
#include "nuggetConf.h"
// see nuggetConf for enum LoraModuleType



// Nugget LoRa addon pins
// SPI pins. RFM95 is MSB first
#define LORA_SCK 15
#define LORA_MISO 17
#define LORA_MOSI 21
#define LORA_SS 34
#define LORA_RST 5
#define LORA_DIO0 16

#define NUGGET_LORA_VERSION 0.1

// this will be replaced with a real checksum at some point

// NUGGET LORA COMMUNICTION CONFIGURATION
//#define LORA_SYNCWORD 0xF3
// defined in .usbnugget.conf

/* communication modes for nugget-nugget communication 
 * LORA_DISABLED - lora communication not enabled - 
 * NUG_COMM_MODE_BASIC - basic controls using buttons, like HID injection
 * NUG_COMM_MODE_CMD - command modes, using yet-to-be-determined syntax
 * RESERVED - reserved for future use
 * On breaking changes, a new protocol mode will be used; this should 
 * avoid incorrect parsing by nuggets running older SW versions
 */
typedef enum LoraCommunicationModes {
    LORA_DISABLED,
    NUG_COMM_MODE_BASIC,
    NUG_COMM_MODE_CMD,
    NUG_WEB_LORA_MESSAGER,
    NUG_COMM_RESERVED1,
    NUG_COMM_RESERVED2
} LoraCommunicationModes;
// number of *IMPLEMENTED* nugget comm modes. used to check if we can parse the packet
#define NUM_NUG_LORA_COMM_MODES 1;    

// standardized set of basic commands that can be used to control another nugget over lora. These will be expanded over time.
enum NuggetLoRaBasicCmds {BTN_UP_CMD, BTN_DN_CMD, BTN_L_CMD, BTN_R_CMD, \
LED_ON_CMD, LED_OFF_CMD, LED_COLOR_CMD, SW_RST_CMD, OLED_ON_CMD, OLED_OFF_CMD, OLED_DISPLAY_TEXT_CMD, \
NUM_LORA_BASIC_CMDS}; // NUM_LORA_BASIC_CMDS must be last, needed to check enum validity
// most of these are simple binary commands - some will need to take arguments to be useful
// LED_COLOR - will need 3x uint8_t
// OLED_DISPLAY_TEXT - line {1..4}, const char* text


/**
 * Message contents of LoRa packet
 * Max len per packet chosen to be 256 bytes
*/
// struct LoraPacketContents {
//   uint8_t length;
//   char* contents;
// };

#define NUG_LORA_PKT_VERSION 1

/**
 * Struct representing LoRa packet
 * @param lora_communication_mode nug_comm_mode this packet should be interpreted as
 *  This field also acts as a version indicator for breaking changes; see above
 * @param version version num 
 * @param destAddr
 * @param srcAddr
 * @param seq - message sequence number
 * @param checksum - TDB checksum for data validity
 * @param payload - actual data we're sending
 */
typedef struct NuggetLoraPacket {
  enum LoraCommunicationModes loraCommunicationMode: 4;
  uint8_t version : 4;
  uint8_t size;
  uint8_t destAddr[2];
  uint8_t srcAddr[2];
  uint8_t seq[2];
  uint8_t payload[0];
  uint8_t checksum;
} __attribute__((packed)) NuggetLoraPacket;



class NuggetLora {
  public: 
    NuggetLora(NuggetConfig conf);
    void initSPI(void);
    bool lora_recv_cb();


    void tx_lora_packet(const NuggetLoraPacket *packet, uint16_t size_b);

    bool initModule(void);

    void sendBasicModePacket(const uint8_t dest_addr[2], \
      const uint8_t payload_size_b, const char *payload);

   private: 
    // uint8_t singleTransfer(uint8_t address, uint8_t value);
    // internal functions
    uint8_t readLoraReg(uint8_t address);
    void lora_packet_recv_cb(void *packetData);
    bool validateLoraConf(NuggetConfig c);

    // internal variables

    uint16_t lora_seq;
    LoRaClass lora;
    SPIClass* spi;

};

/** 
 * Lora configuration struct
 * Holds lora configuration, derived from NuggetConfig options
 * read from .usbnugget.conf
 * @param LoraModuleType enum- RFM95 or RYLR998
 * @param lora_addr local address of lora module
 * @param lora_RFM_syncword - lora protocol syncword
 * @param comm_mode current lora communication mode
 * @param msg_sequence - stored uint16 message sequence value
 * 
 * @note lora_enabled bool from nuggetConf not present here, since that state is represented
 * by option 0 of LoraCommunicationModes enum
*/
typedef struct LoraConfig {
  // bool lora_enabled;
  enum LoraModuleType lora_module_type;
  uint8_t lora_addr[2];
  uint16_t full_lora_addr;  // store lora_addr both in faster-to-send 8b and full 16b format

  int lora_RFM_syncword;
  enum LoraCommunicationModes comm_mode;
  uint16_t msg_sequence;

} LoraConfig;



#endif