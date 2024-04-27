#ifndef NUGGET_CONF_H
#define NUGGET_CONF_H

/**
 * Global nugget configuration types imported across project
*/

// will be def'd by platformio.ini later on
#define DEBUG_LORA 1

// LoraModule type - we suppert RFM95 and RYLR998
enum LoraModuleType {RFM95, RYLR998};

/**
 * Global Nugget Configuration, derived from .usbnugget.conf
 * 
 * @param locale - String 
 * @param network String 
 * @param password String 
 * @param pid long 
 * @param vid long 
 * @param lora_enabled bool - used to gate lora options so they aren't checked when lora disabled
 * @param lora_module_type enum LoraModuleType 
 * @param lora_addr byte 
 * @param lora_RFM_syncword int 
*/
typedef struct NuggetConfig {
  String locale;
  String network;
  String password;
  long pid;
  long vid;
  // Nugget LoRa configuration options
  bool lora_enabled;
  enum LoraModuleType lora_module_type;
  uint16_t lora_addr;
  int lora_RFM_syncword;

} NuggetConfig;



#endif