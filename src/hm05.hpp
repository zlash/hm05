#ifndef HM05_HPP
#define HM05_HPP

#include <ftdi.h>
#include <cassert>

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#define IS_POSIX
#include <time.h>

#elif 
#error Add Non-Posix support 
#endif 


#define VERSION_STRING "v0.0.1"

#define LOG_INFO  1
#define LOG_ERROR 2

#define OUT_BUFFER_SIZE 4 * 1024 * 1024

struct CFIQueryStruct {
  char magicQRY[3];                           // "QRY"
  uint16_t controlInterfaceId;                // (See JEP137)
  uint16_t primaryAlgorithmExtendedTable;     // 0 = No extended table
  uint16_t alternativeControlInterfaceId;     // 0 = No Alt. Control Id
  uint16_t alternativeAlgorithmExtendedTable; // 0 = No Alt. extended table

  uint8_t vccMin;             // BCD Volts: BCD 100mv
  uint8_t vccMax;             // BCD Volts: BCD 100mv
  uint8_t vppMin;             // Volts: BCD 100mv
  uint8_t vppMax;             // Volts: BCD 100mv
  uint8_t typicalTimeouts[8]; // (See JESD68-01)

  uint8_t deviceSize;                      // 2^n bytes
  uint16_t interfaceCodeDescription;       //(See JEP137)
  uint16_t maximumBytesInMultibyteProgram; // 2^n bytes
  uint8_t numberOfEraseBlockRegions;
};

struct CartCommContext {
  ftdi_context *ftdi;
  uint8_t outBuffer[OUT_BUFFER_SIZE];
  int outBufferPos;
  CFIQueryStruct cfiqs;
  uint8_t lowDataBits;
  uint8_t poweredOn;
  uint8_t mpsseOn;
  uint8_t chipId[3];
};

// User must implement this function
void logMessage(int logLevel, const char *formatString, ...);

int openDeviceAndSetupMPSSE(struct ftdi_context *ftdi, CartCommContext *ccc);
int powerOn(CartCommContext *ccc);
int powerOff(CartCommContext *ccc);


void sleepMs(unsigned int ms);

#endif
