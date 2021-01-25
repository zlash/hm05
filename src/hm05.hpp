/*
Copyright (c) 2021 Miguel Ángel Pérez Martínez

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef HM05_HPP
#define HM05_HPP

#include <ftdi.h>
#include <cassert>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define IS_POSIX
#include <time.h>

#elif
#error Add Non-Posix support
#endif

#define VERSION_STRING "v0.0.1"

#define LOG_INFO  1
#define LOG_ERROR 2

#define OUT_BUFFER_SIZE 4 * 1024 * 1024
#define ROM_BUFFER_SIZE 2 * 1024 * 1024

#pragma pack(push, 1)

struct CFIBlockRegion {
  uint16_t nBlocks;   // Number of blocks in region - 1
  uint16_t blockSize; // Size in bytes obtained shifting << 8
};

// TODO: Assuming little-endian types
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
#pragma pack(pop)

struct CartCommContext {
  ftdi_context *ftdi;
  uint8_t outBuffer[OUT_BUFFER_SIZE];
  int outBufferPos;
  CFIQueryStruct cfiqs;
  CFIBlockRegion blockRegions[256];
  uint8_t lowDataBits;
  uint8_t poweredOn;
  uint8_t mpsseOn;
  uint8_t chipId[3];
  uint8_t romBuffer[ROM_BUFFER_SIZE];
  uint32_t biggestBlockSizeBytes;
};

// User must implement this function
void logMessage(int logLevel, const char *formatString, ...);

int openDeviceAndSetupMPSSE(struct ftdi_context *ftdi, CartCommContext *ccc);
int powerOn(CartCommContext *ccc);
int powerOff(CartCommContext *ccc);

int readRom(CartCommContext *ccc);
int writeRom(CartCommContext *ccc, int romSize);

void sleepMs(unsigned int ms);

#endif
