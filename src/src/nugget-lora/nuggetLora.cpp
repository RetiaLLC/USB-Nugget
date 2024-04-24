
/**
 * Interface for Nugget-Nugget Lora communication
 * @author 0xjmux
 * @date April 2024
 * 
*/

#include "nuggetLora.h"




//----------------------------------------
// NuggetLora class

NuggetLora::NuggetLora() {


}

// NuggetLora::initSPI(void) {
//     SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

// }


bool NuggetLora::testForRFM(void) {
    // bits 2-0: https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf#page=102&zoom=auto,-244,691
    #define RFM_RegOpMode_REG 0x01

    uint8_t rfm_opmode = readLoraReg(RFM_RegOpMode_REG);
    Serial.print("RFM opmode: ");
    Serial.println(rfm_opmode);
    // rfm_opmode = rfm_opmode & 0x00000111
    return true;
}

uint8_t NuggetLora::readLoraReg(uint8_t address)
{
  // return singleTransfer(address & 0x7f, 0x00);
  return 0;
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


