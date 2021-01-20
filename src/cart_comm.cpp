#include "hm05.hpp"

#define CHIP_VENDOR  0x0403
#define CHIP_PRODUCT 0x6010

#define CALL_FTDI(CMD, ERROR, ...)                                             \
  {                                                                            \
    int ret;                                                                   \
    if ((ret = CMD(ftdi, ##__VA_ARGS__)) < 0) {                                \
      logMessage(                                                              \
        LOG_ERROR, "%s: %d (%s)\n", ERROR, ret, ftdi_get_error_string(ftdi));  \
      ftdi_usb_close(ftdi);                                                    \
      ftdi_free(ftdi);                                                         \
      return -1;                                                               \
    }                                                                          \
  }

#define flushOut()                                                             \
  if (flushOut_(ftdi) < 0) {                                                   \
    return -1;                                                                 \
  }

#define OUT_BUFFER_SIZE 4 * 1024 * 1024
static uint8_t outBuffer[OUT_BUFFER_SIZE];
static int outBufferPos = 0;

inline void outByte(uint8_t byte) {
  assert(outBufferPos < OUT_BUFFER_SIZE);
  outBuffer[outBufferPos++] = byte;
}

inline int flushOut_(struct ftdi_context *ftdi) {
  CALL_FTDI(
    ftdi_write_data, "Unable to write data to device", outBuffer, outBufferPos);
  outBufferPos = 0;
  return 0;
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
                 "%s: %d (%s)\n",
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

// TODO: Opens first device matching descriptor for now. Allow listing and
// selecting devices
int openDeviceAndSetupMPSSE(struct ftdi_context *ftdi) {

  {
    int ret;
    if ((ret = ftdi_usb_open(ftdi, CHIP_VENDOR, CHIP_PRODUCT)) != 0) {
      logMessage(LOG_ERROR,
                 "Unable to open ftdi device: %d (%s)\n",
                 ret,
                 ftdi_get_error_string(ftdi));
      ftdi_free(ftdi);
      return -1;
    }
  }

  // Steps following the oficial guide to setup MPSSE
  //------------------------------

  // Reset device
  CALL_FTDI(ftdi_usb_reset, "Unable to reset device");

  // Set chunk sizes to 64KiB
  const int ChunkSize = 65536;

  CALL_FTDI(
    ftdi_write_data_set_chunksize, "Unable to set write chunk size", ChunkSize);
  CALL_FTDI(
    ftdi_read_data_set_chunksize, "Unable to set read chunk size", ChunkSize);

  // Set timeout that is used to flush remaining data from the receive buffer in
  // milliseconds.
  CALL_FTDI(ftdi_set_latency_timer, "Unable to set latency", 2);

  // Turn on flow control so no read requests are generated while buffer is full
  CALL_FTDI(ftdi_setflowctrl, "Unable to turn on flow control", SIO_RTS_CTS_HS);

  // Reset controller
  CALL_FTDI(ftdi_set_bitmode, "Unable to reset controller", 0x00, 0x00);
  // Enable MPSSE mode
  CALL_FTDI(ftdi_set_bitmode, "Unable to enable MPSSE mode", 0x00, 0x02);

  // Check if MPSSE is working
  //
  // If a bad command is detected, the MPSSE returns the value 0xFA, followed by
  // the byte that caused the bad command.Use of the bad command detection is
  // the recommended method of determining whether the MPSSE is in sync with the
  // application program.  By sending a bad command on purpose and looking for
  // 0xFA, the application can determine whether communication with the MPSSE is
  // possible.
  //

  // Enable loopback
  outByte(0x84);
  flushOut();

  // Flush readbuffer
  CALL_FTDI(ftdi_usb_purge_rx_buffer, "Unable to flush read buffer");

  // Send bogus command
  outByte(0xAB);
  flushOut();

  uint8_t bogusCommandLoopback[2];

  readSync(bogusCommandLoopback, 2);

  if (bogusCommandLoopback[0] != 0xFA || bogusCommandLoopback[0] != 0xFA) {
    logMessage(LOG_ERROR, "MPSSE Validation Failed");
    return -1;
  }

  // Disable loopback
  outByte(0x85);
  flushOut();

  // Flush readbuffer
  CALL_FTDI(ftdi_usb_purge_rx_buffer, "Unable to flush read buffer");

  // Device pins directions
  // ------------------------------
  //
  // ADBUS0 TCK/SK  output  1
  // ADBUS1 TDI/DO  output  1
  // ADBUS2 TDO/DI  input   0
  // ADBUS3 TMS/CS  output  1
  // ADBUS4 POWER   output  1
  // ADBUS5 GPIOL1  input   0
  // ADBUS6 GPIOL2  input   0
  // ADBUS7 ISPOWER input   0

  const uint8_t ADBUSInitialValue = 0x18; // Power = 1, CS=1
  const uint8_t ADBUSDirections = 0x1B;

  // Set ADBUS value and pins direction
  outByte(0x80);              // Command
  outByte(ADBUSInitialValue); // Value
  outByte(ADBUSDirections);   // Directions
  flushOut();

  // Use 60MHz master clock (disable divide by 5)
  outByte(0x8A);

  // Set TCK/SK Clock divisor
  // TCK/SK period = 60MHz  /  (( 1 +[ (0xValueH * 256) OR 0xValueL] ) * 2)
  // For 15MHz: 0x0001
  outByte(0x86); // Command
  outByte(0x01); // ValueL
  outByte(0x00); // ValueH
  flushOut();

  logMessage(LOG_INFO, "Device ready.");

  return 0;
}