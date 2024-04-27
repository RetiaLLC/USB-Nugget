
/**
 * Interface for Nugget-Nugget Lora communication
 * @author 0xjmux
 * @date April 2024
 * 
*/

#include "nuggetLora.h"


LoraConfig lora_conf;

//----------------------------------------
// NuggetLora class constructor - read 
//  config options from conf into lora_Conf
NuggetLora::NuggetLora(NuggetConfig conf) {
  lora_conf.lora_module_type = conf.lora_module_type;
  lora_conf.lora_addr = conf.lora_addr;
  lora_conf.lora_RFM_syncword = conf.lora_RFM_syncword;


}


void NuggetLora::initSPI() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

    if (testForRFM()) {
      Serial.println("RFM module detected on SPI bus!");
    }
    else {
      Serial.println("RFM module NOT detected on SPI bus!");
    }
}


bool NuggetLora::testForRFM(void) {
    // bits 2-0: https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf#page=102&zoom=auto,-244,691
    #define RFM_RegOpMode_REG 0x01

    uint8_t rfm_opmode = readLoraReg(RFM_RegOpMode_REG);
    Serial.print("RFM opmode: ");
    Serial.println(rfm_opmode);
    // rfm_opmode = rfm_opmode & 0x00000111
    return true;
}


/**
 * Function to read specific register of LoRa module to test if present on SPI bus
*/
uint8_t NuggetLora::readLoraReg(uint8_t address)
{
  if (lora_conf.lora_module_type == RFM95) {
    true;
  }
  // return singleTransfer(address & 0x7f, 0x00);
  return 0;
}

/**
 * Transmit lora packet
*/
void NuggetLora::tx_lora_packet(NuggetLoraPacket *packet, uint16_t size_b) {
  LoRa.beginPacket();                   // start packet

  
  for (uint16_t i = 0; i < size_b; i++) {
    // not sure if these are 8 bit words, so this could be possible issue
    // LoRa.write(packet[i]);

  }
  LoRa.endPacket();                     // finish packet and send it
  lora_seq++;                           // increment message ID

  // LoRa.write(destination);              // add destination address
  // LoRa.write(localAddress);             // add sender address
  // LoRa.write(msgCount);                 // add message ID
  // LoRa.write(outgoing.length());        // add payload length
  // LoRa.print(outgoing);                 // add payload
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
  if (recipient != lora_conf.lora_addr && recipient != 0xFF) {
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



bool NuggetLora::testForRYLR(void) {
  // test for presece of RYRL998 on SW UART bus
  // IMPLEMENT
  return false;
}


/**
 * Sanity check lora components of NuggetConfig
*/
bool NuggetLora::validateLoraConf(NuggetConfig c) {

  return true;
}



// void LoRaClass::writeRegister(uint8_t address, uint8_t value)
// {
//   singleTransfer(address | 0x80, value);
// }

// uint8_t LoRaClass::singleTransfer(uint8_t address, uint8_t value)
// {
//   uint8_t response;

//   _spi->beginTransaction(_spiSettings);
//   digitalWrite(_ss, LOW);
//   _spi->transfer(address);
//   response = _spi->transfer(value);
//   digitalWrite(_ss, HIGH);
//   _spi->endTransaction();

//   return response;
// }


