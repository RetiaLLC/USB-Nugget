
/**
 * USBNugget Lora communication library
 * @author 0xjmux
 * @date April 2024
 * 
*/

/**
 * Goals:
 * *  same set of functions should work for either module. We haven't gotten module 
 *    interoperability working yet, but eventually we need to get that working. 
 *    This means that functions that are different for each module must check 
 *    which module is active. 
*/

/** TODO:
 * find reliable way of doing message sequencing that allows for flexible 
 *  nugget-nugget communication
*/


#include "nuggetLora.h"

// convert uint16 to uint8:
//  uint8_t hi_lo[] = { (uint8_t)(value >> 8), (uint8_t)value }; // { 0xAA, 0xFF }

LoraConfig lora_conf;

//----------------------------------------
// NuggetLora class constructor - read 
//  config options from conf into lora_Conf
NuggetLora::NuggetLora(NuggetConfig conf) {
  lora_conf.lora_module_type = conf.lora_module_type;
  lora_conf.full_lora_addr = conf.lora_addr;
  // config loads lora_addr as a uint16_t, but we store it here as 2 uint8_ts for ease of use
  lora_conf.lora_addr[1] = (conf.lora_addr >> 8);   // MSB 8 bits
  lora_conf.lora_addr[0] = (0x00FF && conf.lora_addr);

  lora_conf.lora_RFM_syncword = conf.lora_RFM_syncword;
  // this is temporary, since we only have 1 comm mode implemented rn
  // we just check if we're enabled or not
  lora_conf.comm_mode = (conf.lora_enabled) ? NUG_COMM_MODE_BASIC : LORA_DISABLED;
  lora_conf.msg_sequence = 1;

  #ifdef DEBUG_LORA
    Serial.printf("config loaded uint16 lora_addr=%d, 2xUint8 lora_addr=%d %d\n", \
      conf.lora_addr, lora_conf.lora_addr[1], lora_conf.lora_addr[0]);
  #endif

}


/**
 * Initialize module pins, and check for presence on the bus. 
 * @returns true if successful, false otherwise
*/
bool NuggetLora::initModule(void) {

  switch (lora_conf.lora_module_type) {
    case RFM95:

      SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

      if (LoRa.begin(915E6)) {         // initialize ratio at 915 MHz
        Serial.println("Successfully found RFM95 on SPI bus");
        return true;
      }
      else {
        Serial.println("Could not detect RFM95 on SPI bus.");
        return false;
      }
      break;
    case RYLR998:
      Serial.println("RYLR998 not IMPL Yet, cannot init!");
      break;
  }
  Serial.println("Cannot init invalid lora module type, exiting!");
  return false;
}


/**
 * Transmit lora packet
*/
void NuggetLora::tx_lora_packet(const NuggetLoraPacket *packet, uint16_t size_b) {
  #ifdef DEBUG_LORA
  Serial.printf("building lora packet for TX with size %d now\n", size_b);
  #endif


  LoRa.beginPacket();                   // start packet
  // LoRa.write(packet, size_b);
//  for (uint16_t i = 0; i < size_b; i++) {
//    // not sure if these are 8 bit words, so this could be possible issue
//    LoRa.write((byte) packet[i]);
//  }
  LoRa.endPacket();                     // finish packet and send it
  lora_seq++;                           // increment message ID

  // LoRa.write(destination);              // add destination address
  // LoRa.write(localAddress);             // add sender address
  // LoRa.write(msgCount);                 // add message ID
  // LoRa.write(outgoing.length());        // add payload length
  // LoRa.print(outgoing);                 // add payload
}

/**
 * Send a NUG_COMM_MODE_BASIC packet
*/
void NuggetLora::sendBasicModePacket(const uint8_t dest_addr[2], \
  const uint8_t payload_size_b, const char *payload) 
{
  // sanity check
  if (lora_conf.comm_mode != NUG_COMM_MODE_BASIC) {
    Serial.printf("Unable to send basic packet since comm mode is: %d!", lora_conf.comm_mode);
    return;
  }
  assert(payload_size_b < 256);   // should always be true, but nevertheless

  // combine two 4b fields into one uint8
  uint8_t combinedCommModeVersion = (lora_conf.comm_mode << 4) + NUG_LORA_PKT_VERSION;

  // FOR THIS FIRST VERY PRIMITIVE VERSION CHECKSUM IS JUST PAYLOAD SIZE
  uint8_t checksum = payload_size_b;


  #ifdef DEBUG_LORA
    Serial.printf("Sending basic mode packet: DEST:%d%d  SRC:%d%d\n", dest_addr[1], \
    dest_addr[0], lora_conf.lora_addr[1], lora_conf.lora_addr[0]);
    char combinedCommModeString[9];
    itoa(combinedCommModeVersion, combinedCommModeString, 2);
    Serial.printf("Combined CommMode/Version: %s, payload_size: %d\n", combinedCommModeString, payload_size_b);
    Serial.printf("Seq = %d, checksum = %d\n", lora_conf.msg_sequence, checksum);

  
  #endif

  switch (lora_conf.lora_module_type) {
    case RFM95:

      // while this method is more conveinent not using NuggetLoraPacket during 
      //  packet creation opens up possible compatibility issues
      LoRa.beginPacket();                   // start packet
      // MSB
      LoRa.write(combinedCommModeVersion);
      LoRa.write(payload_size_b);
      LoRa.write(dest_addr[1]);
      LoRa.write(dest_addr[0]);
      LoRa.write(lora_conf.lora_addr[1]);
      LoRa.write(lora_conf.lora_addr[0]);
      LoRa.write((uint8_t)(lora_conf.msg_sequence >> 8));
      LoRa.write((uint8_t) lora_conf.msg_sequence);
      for (int i = 0; i < payload_size_b; i++) {
        LoRa.write((uint8_t) payload[i]);
      }
      LoRa.write(checksum);
      LoRa.endPacket();                     // finish packet and send it
      lora_conf.msg_sequence += 1;                           // increment message ID

      break;
    case RYLR998:
      Serial.println("RYLR NOT IMPL YET");
      return;
      break;

    default:
        Serial.println("Cannot construct packet for invalid lora module type!");

      break;

  }
}


/**
 * On receive, convert received packet data to NuggetLoraPacket type
 * and verify validity
 * IDK IF VOID PTR IS RIGHT CHOICE HERE
*/
void NuggetLora::lora_packet_recv_cb(void *packetData) {
  return;

}


//bool lora_recv_cb();


// taken from LoRaDuplex.ino
void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  // THIS WILL NOT WORK WITH MY IMPL
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }


  // if the recipient isn't this device or broadcast,
  if (recipient != lora_conf.full_lora_addr && recipient != 0xFF) {
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }

  // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
}




/**
 * Sanity check lora components of NuggetConfig
*/
bool NuggetLora::validateLoraConf(NuggetConfig c) {

  return true;
}




