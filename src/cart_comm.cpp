#include "hm05.hpp"
#include <cstring>

#define CHIP_VENDOR  0x0403
#define CHIP_PRODUCT 0x6010

//  Device Pins Setup
//  -------------
//  Bit0 = CLK          Out
//  Bit1 = DO           Out
//  Bit2 = DI           In
//  Bit3 = CS           Out
//  Bit4 = POWER_CTRL   Out <- Active low
//  Bit5 = none         In
//  Bit6 = none         In
//  Bit7 = IS_POWER_ON  In

#define CS_BIT    0x08
#define POWER_BIT 0x10

#define SET_BITS(DST, BITS)   ((DST) | (BITS))
#define UNSET_BITS(DST, BITS) ((DST) & (~(BITS)))

const uint8_t ADBUSDirections = 0x1B;
const int latencyMs = 2;

enum SST39VF168XCommand {
  SST_CHIP_ID,
  SST_CFI_QUERY_MODE,
  SST_EXIT_TO_READ_MODE,
  SST_WRITE_BYTE,
  SST_BLOCK_ERASE,
  SST_END,
};

#define assertInBufferEmpty()                                                  \
  {                                                                            \
    int ret;                                                                   \
    if ((ret = flushIn(ccc)) != 0) {                                           \
      logMessage(LOG_ERROR,                                                    \
                 "[%s:%d] Out of sync: Expected empty buffer (%d). Aborting.", \
                 __FILE__,                                                     \
                 __LINE__,                                                     \
                 ret);                                                         \
      return -1;                                                               \
    }                                                                          \
  }

#define CALL_FTDI(CMD, ERROR, ...)                                             \
  {                                                                            \
    int ret;                                                                   \
    if ((ret = CMD(ftdi, ##__VA_ARGS__)) < 0) {                                \
      logMessage(                                                              \
        LOG_ERROR, "%s: %d (%s)", ERROR, ret, ftdi_get_error_string(ftdi));    \
      ftdi_usb_close(ftdi);                                                    \
      ftdi_free(ftdi);                                                         \
      return -1;                                                               \
    }                                                                          \
  }

int setLowDataBits(CartCommContext *ccc, uint8_t bits);

// 64-bits systems only
inline uint8_t reverseByte(uint8_t b) {
  return (b * 0x0202020202ULL & 0x010884422010ULL) % 1023;
}

inline void setCS(CartCommContext *ccc, uint8_t high) {
  if (high) {
    setLowDataBits(ccc, SET_BITS(ccc->lowDataBits, CS_BIT));
  } else {
    setLowDataBits(ccc, UNSET_BITS(ccc->lowDataBits, CS_BIT));
  }
  sleepMs(1);
}

inline void enqueueByteOut(CartCommContext *ccc, uint8_t byte) {
  assert(ccc->outBufferPos < OUT_BUFFER_SIZE);
  ccc->outBuffer[ccc->outBufferPos++] = byte;
}

inline int flushOut(CartCommContext *ccc) {
  assert(ccc->outBufferPos > 0);
  auto ftdi = ccc->ftdi;
  CALL_FTDI(ftdi_write_data,
            "Unable to write data to device",
            ccc->outBuffer,
            ccc->outBufferPos);
  sleepMs(latencyMs + 1);
  ccc->outBufferPos = 0;
  return 0;
}

inline int flushIn(CartCommContext *ccc) {
  const int flushBlockSize = 1024;
  auto ftdi = ccc->ftdi;
  uint8_t bytesRead = 0;

  uint8_t buff[flushBlockSize];

  for (;;) {
    int ret = ftdi_read_data(ftdi, buff, flushBlockSize);
    if (ret < 0) {
      logMessage(LOG_ERROR,
                 "%s: %d (%s)",
                 "Unable to read data",
                 ret,
                 ftdi_get_error_string(ftdi));
      ftdi_usb_close(ftdi);
      ftdi_free(ftdi);
      return -1;
    }
    if (ret == 0) {
      break;
    }
    bytesRead += ret;
  }

  return bytesRead;
}

#define readSync(DST, NBYTES)                                                  \
  if (readSync_(ftdi, (DST), (NBYTES)) < 0) {                                  \
    return -1;                                                                 \
  }

// Keeps reading until the desired amount of bytes was actually read.
int readSync_(struct ftdi_context *ftdi, uint8_t *dst, int nBytes) {
  uint8_t *curDst = dst;
  int missingBytes = nBytes;

  for (;;) {
    int ret = ftdi_read_data(ftdi, curDst, missingBytes);
    if (ret < 0) {
      logMessage(LOG_ERROR,
                 "%s: %d (%s)",
                 "Unable to read data",
                 ret,
                 ftdi_get_error_string(ftdi));
      ftdi_usb_close(ftdi);
      ftdi_free(ftdi);
      return -1;
    }
    missingBytes -= ret;
    curDst += ret;
    if (missingBytes == 0) {
      break;
    }
  }
  return nBytes;
}

int readFlash(CartCommContext *ccc,
              int addr,
              uint8_t *dst,
              int nBytes,
              uint8_t reverseBytes = 0) {
  setCS(ccc, 0);
  sleepMs(1);

  auto ftdi = ccc->ftdi;

  // It seems that until I read from the device, the buffer keeps filling
  // and when it's full, the write fails.
  // The buffer is quite small so read requests need to be splitted

  const int readRequestSize = 256;

  while (nBytes > 0) {
    const int bytesToRead = nBytes > readRequestSize ? readRequestSize : nBytes;
    nBytes -= bytesToRead;

    for (int i = 0; i < bytesToRead; i++) {
      // Clock Data Bytes Out on -ve clock edge MSB first  (no read)
      enqueueByteOut(ccc, 0x11); // Command
      enqueueByteOut(ccc, 0x02); // (NBytes - 1) L
      enqueueByteOut(ccc, 0x00); // (NBytes - 1) H

      // Load 3 bytes address
      enqueueByteOut(ccc, (addr >> 16) & 0x1F);
      enqueueByteOut(ccc, (addr >> 8) & 0xFF);
      enqueueByteOut(ccc, addr & 0xFF);

      // Clock Data Bytes In on -ve clock edge MSB first (no write)
      enqueueByteOut(ccc, 0x24); // Command
      enqueueByteOut(ccc, 0x00); // (NBytes - 1) L
      enqueueByteOut(ccc, 0x00); // (NBytes - 1) H
      addr++;
    }

    // Force receive current readbuffer contents from chip
    enqueueByteOut(ccc, 0x87);
    flushOut(ccc);

    readSync(dst, bytesToRead);

    // Reverse bytes
    if (reverseBytes) {
      for (int i = 0; i < bytesToRead; i++) {
        dst[i] = reverseByte(dst[i]);
      }
    }

    dst += bytesToRead;
  }
  return 0;
}

int setLowDataBits(CartCommContext *ccc, uint8_t bits) {
  ccc->lowDataBits = bits;
  enqueueByteOut(ccc, 0x80);             // Command
  enqueueByteOut(ccc, ccc->lowDataBits); // Value
  enqueueByteOut(ccc, ADBUSDirections);  // Directions
  return flushOut(ccc);
}

void enqueueFlashOut(CartCommContext *ccc, int addr, uint8_t data) {
  // Clock Data Bytes Out on -ve clock edge MSB first  (no read)
  enqueueByteOut(ccc, 0x11); // Command
  enqueueByteOut(ccc, 0x03); // (NBytes - 1) L
  enqueueByteOut(ccc, 0x00); // (NBytes - 1) H

  // Layout:
  //    2 1111 1111 1100 000000000   0000 0000
  //    0 9876 5432 1098 7654 3210   7654 3210
  // CxxA AAAA AAAA AAAA AAAA AAAA | DDDD DDDD

  // Address (21 bits) and data
  enqueueByteOut(ccc, ((addr >> 16) & 0x1F) | 0x80); // Write flag (0x80)
  enqueueByteOut(ccc, (addr >> 8) & 0xFF);
  enqueueByteOut(ccc, addr & 0xFF);
  enqueueByteOut(ccc, data);
}

void enqueueSST39VF168XCommand(CartCommContext *ccc,
                               SST39VF168XCommand command,
                               int param1 = 0,
                               int param2 = 0) {
  assert(command < SST_END);

  // All comands share this first two address/data combinations
  enqueueFlashOut(ccc, 0xAAA, 0xAA);
  enqueueFlashOut(ccc, 0x555, 0x55);

  switch (command) {
    case SST_CHIP_ID:
      enqueueFlashOut(ccc, 0xAAA, 0x90);
      break;
    case SST_CFI_QUERY_MODE:
      enqueueFlashOut(ccc, 0xAAA, 0x98);
      break;
    case SST_EXIT_TO_READ_MODE:
      enqueueFlashOut(ccc, 0xAAA, 0xF0);
      break;
    case SST_WRITE_BYTE:
      enqueueFlashOut(ccc, 0xAAA, 0xA0);
      enqueueFlashOut(ccc, param1, reverseByte(param2));
      break;
    case SST_BLOCK_ERASE:
      enqueueFlashOut(ccc, 0xAAA, 0x80);
      enqueueFlashOut(ccc, 0xAAA, 0xAA);
      enqueueFlashOut(ccc, 0x555, 0x55);
      enqueueFlashOut(ccc, param1, 0x30);
      break;
  }
}

int writeSST39VF168XCommand(CartCommContext *ccc,
                            SST39VF168XCommand command,
                            int param1 = 0,
                            int param2 = 0) {
  enqueueSST39VF168XCommand(ccc, command, param1, param2);

  flushOut(ccc);
  assertInBufferEmpty();
  sleepMs(1);

  return 0;
}

// TODO: Dump the rest of the fields
void dumpCFIDataToLog(const CartCommContext *ccc) {
  auto cfiqs = &ccc->cfiqs;
  const int64_t sizeBytes = 1 << cfiqs->deviceSize;

  logMessage(LOG_INFO,
             "Flash CFI Magic Header: %c%c%c",
             cfiqs->magicQRY[0],
             cfiqs->magicQRY[1],
             cfiqs->magicQRY[2]);

  logMessage(LOG_INFO,
             "Flash CFI Reported device size: %d bytes (%d MiB)",
             sizeBytes,
             sizeBytes / (1024 * 1024));

  logMessage(
    LOG_INFO, "Number of block regions: %d", cfiqs->numberOfEraseBlockRegions);
  for (int i = 0; i < cfiqs->numberOfEraseBlockRegions; i++) {
    logMessage(LOG_INFO, "Block region #%d", i + 1);
    logMessage(
      LOG_INFO, "\tBlock size: %d bytes", ccc->blockRegions[i].blockSize << 8);
    logMessage(
      LOG_INFO, "\tBlocks in region: %d", ccc->blockRegions[i].nBlocks + 1);
  }

  logMessage(
    LOG_INFO, "Using block size: %d bytes", ccc->biggestBlockSizeBytes);
}

int readChipId(CartCommContext *ccc) {
  if (writeSST39VF168XCommand(ccc, SST_CHIP_ID) < 0) {
    return -1;
  }

  if (readFlash(ccc, 0x0, ccc->chipId, 3) < 0) {
    logMessage(LOG_ERROR, "Chip Id read failed.");
    return -1;
  }

  if (writeSST39VF168XCommand(ccc, SST_EXIT_TO_READ_MODE) < 0) {
    return -1;
  }

  return 0;
}

int readCFIQueryStruct(CartCommContext *ccc) {
  auto ftdi = ccc->ftdi;
  auto cfiqs = &ccc->cfiqs;

  if (writeSST39VF168XCommand(ccc, SST_CFI_QUERY_MODE) < 0) {
    return -1;
  }

  if (readFlash(ccc, 0x10, (uint8_t *)cfiqs, sizeof(CFIQueryStruct)) < 0) {
    logMessage(LOG_ERROR, "Flash CFI Query struct flash read failed.");
    return -1;
  }

  if (readFlash(ccc,
                0x2D,
                (uint8_t *)ccc->blockRegions,
                sizeof(CFIBlockRegion) * cfiqs->numberOfEraseBlockRegions) <
      0) {
    logMessage(LOG_ERROR, "Flash CFI block regions read failed.");
    return -1;
  }

  if (writeSST39VF168XCommand(ccc, SST_EXIT_TO_READ_MODE) < 0) {
    return -1;
  }

  if (cfiqs->magicQRY[0] != 'Q' || cfiqs->magicQRY[1] != 'R' ||
      cfiqs->magicQRY[2] != 'Y') {
    logMessage(LOG_ERROR, "CFI query failed");
    return -1;
  }

  assert(cfiqs->numberOfEraseBlockRegions > 0);

  // Find largest block size and use it
  ccc->biggestBlockSizeBytes = ccc->blockRegions[0].blockSize << 8;
  for (int i = 1; i < cfiqs->numberOfEraseBlockRegions; i++) {
    uint32_t currentSize = ccc->blockRegions[i].blockSize << 8;
    if (currentSize > ccc->biggestBlockSizeBytes) {
      ccc->biggestBlockSizeBytes = currentSize;
    }
  }

  return 0;
}

int powerOn(CartCommContext *ccc) {
  if (!ccc->poweredOn) {
    setCS(ccc, 1);
    setLowDataBits(ccc, UNSET_BITS(ccc->lowDataBits, POWER_BIT));
    setCS(ccc, 0);
    ccc->poweredOn = 1;
  }
  return 0;
}

int powerOff(CartCommContext *ccc) {
  if (ccc->poweredOn) {
    setLowDataBits(ccc, SET_BITS(ccc->lowDataBits, POWER_BIT));
    ccc->poweredOn = 0;
  }
  return 0;
}

int writeRom(CartCommContext *ccc, int romSize) {
  // NOTE: Refactor if bigger chip size is ever used
  assert((1 << ccc->cfiqs.deviceSize) <= ROM_BUFFER_SIZE);
  assert(romSize <= ROM_BUFFER_SIZE);

  int blockNumber = 0;
  int addr = 0;
  uint8_t *src = ccc->romBuffer;
  int remainingBytes = romSize;

  uint8_t *readBackBuffer = new uint8_t[ccc->biggestBlockSizeBytes];

  logMessage(LOG_INFO, "Writing %d bytes", romSize);
  while (remainingBytes > 0) {
    const int bytesToWrite = remainingBytes > ccc->biggestBlockSizeBytes
                               ? ccc->biggestBlockSizeBytes
                               : remainingBytes;
    remainingBytes -= bytesToWrite;

    // Erase block
    //------------------------------
    writeSST39VF168XCommand(ccc, SST_BLOCK_ERASE, addr, 0);
    logMessage(LOG_INFO, "ROM Block %d erased", blockNumber + 1);

    // Write block
    //------------------------------

    for (int i = 0; i < bytesToWrite; i++) {
      enqueueSST39VF168XCommand(ccc, SST_WRITE_BYTE, addr + i, src[i]);
    }

    if (flushOut(ccc) < 0) {
      logMessage(LOG_ERROR, "ROM block %d write failed", blockNumber + 1);
      return -1;
    }

    assertInBufferEmpty();
    logMessage(LOG_INFO, "ROM Block %d written", blockNumber + 1);

    // Read back block
    //------------------------------
    if (readFlash(ccc, addr, readBackBuffer, bytesToWrite, 1) < 0) {
      logMessage(
        LOG_ERROR, "ROM block %d verification read failed", blockNumber + 1);
      return -1;
    }

    // Verify
    //------------------------------
    if (memcmp(readBackBuffer, src, bytesToWrite) != 0) {
      logMessage(
        LOG_ERROR, "ROM block %d verification failed", blockNumber + 1);
      // return -1;
    }

    logMessage(LOG_INFO, "ROM Block %d verified", blockNumber + 1);
    blockNumber++;

    src += bytesToWrite;
    addr += bytesToWrite;
  }

  delete[] readBackBuffer;

  logMessage(LOG_INFO, "ROM write completed");
}

int readRom(CartCommContext *ccc) {
  // TODO: Refactor if bigger chip size is ever used
  assert((1 << ccc->cfiqs.deviceSize) <= ROM_BUFFER_SIZE);

  int numBlocks = (1 << ccc->cfiqs.deviceSize) / ccc->biggestBlockSizeBytes;
  for (int i = 0; i < numBlocks; i++) {
    const int addr = i * ccc->biggestBlockSizeBytes;
    if (readFlash(
          ccc, addr, &ccc->romBuffer[addr], ccc->biggestBlockSizeBytes, 1) <
        0) {
      logMessage(LOG_ERROR, "Cart ROM read failed");
      return -1;
    }
    logMessage(LOG_INFO, "Read block: %d/%d", i + 1, numBlocks);
  }

  logMessage(LOG_INFO, "ROM read completed");
}

// TODO: Opens first device matching descriptor for now. Allow listing and
// selecting devices
int openDeviceAndSetupMPSSE(struct ftdi_context *ftdi, CartCommContext *ccc) {
  ftdi->usb_write_timeout = 10000;
  ftdi->usb_read_timeout = 10000;
  // Init context
  ccc->mpsseOn = 0;
  ccc->poweredOn = 0;

  {
    int ret;
    if ((ret = ftdi_usb_open(ftdi, CHIP_VENDOR, CHIP_PRODUCT)) != 0) {
      logMessage(LOG_ERROR,
                 "Unable to open ftdi device: %d (%s)",
                 ret,
                 ftdi_get_error_string(ftdi));
      ftdi_free(ftdi);
      return -1;
    }
  }

  // Init CartCommContext
  ccc->ftdi = ftdi;
  ccc->outBufferPos = 0;

  // Steps following the oficial guide to setup MPSSE
  //------------------------------

  // Reset device
  CALL_FTDI(ftdi_usb_reset, "Unable to reset device");

  // Set chunk sizes to 64KiB
  const int chunkSize = 65536;

  CALL_FTDI(
    ftdi_write_data_set_chunksize, "Unable to set write chunk size", chunkSize);
  CALL_FTDI(
    ftdi_read_data_set_chunksize, "Unable to set read chunk size", chunkSize);

  // Disable special chars
  CALL_FTDI(ftdi_set_event_char, "Unable to reset event char", 0, 0);
  CALL_FTDI(ftdi_set_error_char, "Unable to reset error char", 0, 0);

  // Set timeout that is used to flush remaining data from the receive buffer in
  // milliseconds.
  CALL_FTDI(ftdi_set_latency_timer, "Unable to set latency", latencyMs);

  // Turn on flow control so no read requests are generated while buffer is full
  CALL_FTDI(ftdi_setflowctrl, "Unable to turn on flow control", SIO_RTS_CTS_HS);

  // Reset controller
  CALL_FTDI(ftdi_set_bitmode, "Unable to reset controller", 0x00, 0x00);
  // Enable MPSSE mode
  CALL_FTDI(ftdi_set_bitmode, "Unable to enable MPSSE mode", 0x00, 0x02);

  // Sync with MPSSE
  //
  // If a bad command is detected, the MPSSE returns the value 0xFA, followed by
  // the byte that caused the bad command.Use of the bad command detection is
  // the recommended method of determining whether the MPSSE is in sync with the
  // application program.  By sending a bad command on purpose and looking for
  // 0xFA, the application can determine whether communication with the MPSSE is
  // possible.
  //

  // Enable loopback
  enqueueByteOut(ccc, 0x84);
  flushOut(ccc);

  assertInBufferEmpty();

  // Send bogus command
  enqueueByteOut(ccc, 0xAB);
  flushOut(ccc);

  uint8_t bogusCommandLoopback[2];

  readSync(bogusCommandLoopback, 2);

  if (bogusCommandLoopback[0] != 0xFA || bogusCommandLoopback[1] != 0xAB) {
    logMessage(LOG_ERROR, "MPSSE Sync Failed");
    return -1;
  }

  logMessage(LOG_INFO, "MPSSE On Sync");

  // Disable loopback
  enqueueByteOut(ccc, 0x85);
  flushOut(ccc);
  assertInBufferEmpty();

  // My FTDI device doesn't support the following commands
  // TODO: Try doing a version/feature check and enable them conditionally

  /*
  // Use 60MHz master clock (disable divide by 5)
  enqueueByteOut(ccc, 0x8A);

  // Turn off adaptive clocking
  enqueueByteOut(ccc, 0x97);

  // Disable three phase clocking
  enqueueByteOut(ccc, 0x8D);

  flushOut(ccc);
  assertInBufferEmpty();
  */

  // Set TCK/SK Clock divisor
  // TODO: This is different if divide by 5 was disabled (60Mhz)
  // TCK/SK period = 12MHz  /  (( 1 +[ (0xValueH * 256) OR 0xValueL] ) * 2)
  // For 3MHz: 0x0000
  enqueueByteOut(ccc, 0x86); // Command
  enqueueByteOut(ccc, 0x01); // ValueL
  enqueueByteOut(ccc, 0x00); // ValueH
  flushOut(ccc);
  assertInBufferEmpty();

  sleepMs(10);
  ccc->mpsseOn = 1;
  logMessage(LOG_INFO, "FTDI Device Ready");

  // Power on!
  powerOn(ccc);
  assertInBufferEmpty();
  logMessage(LOG_INFO, "Programmer powered on");

  // Check chip info
  if (readChipId(ccc) < 0) {
    logMessage(LOG_ERROR, "Flash chip ID Read Failed");
    return -1;
  }

  logMessage(LOG_INFO, "Flash chip Manufacturer Id: %X", ccc->chipId[0]);
  logMessage(LOG_INFO, "Flash chip Device Id: %X", ccc->chipId[1]);

  // TODO: Might need refactor if different chips are used
  // Validate Chip Id Info
  if (ccc->chipId[0] != 0xBF) {
    logMessage(LOG_ERROR, "Wrong chip manufacturer");
    return -1;
  }

  if (ccc->chipId[1] != 0xC8) {
    logMessage(LOG_ERROR, "Wrong chip manufacturer");
    return -1;
  }

  if (readCFIQueryStruct(ccc) < 0) {
    logMessage(LOG_ERROR, "Read CFI Query Struct failed");
    return -1;
  }

  dumpCFIDataToLog(ccc);

  logMessage(LOG_INFO, "Flash chip ready");

  return 0;
}
