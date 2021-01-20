#include <ftdi.h>

#ifndef logError
#define logError(STR, ...)
#endif

#define CHIP_VENDOR  0x0403
#define CHIP_PRODUCT 0x6001

#define CALL_FTDI(CMD, ERROR, ...)                                             \
  if ((ret = CMD(ftdi __VA_OPT__(, ) __VA_ARGS__)) != 0) {                     \
    logError("%s: %d (%s)\n", ERROR, ret, ftdi_get_error_string(ftdi));        \
    ftdi_usb_close(ftdi);                                                      \
    ftdi_free(ftdi);                                                           \
    return -1;                                                                 \
  }

// TODO: Opens first device matching descriptor for now. Allow listing and
// selecting devices

int openDeviceAndSetupMPSSE(struct ftdi_context *ftdi) {
  int ret;

  CALL_FTDI(
    ftdi_usb_open, "Unable to open ftdi device", CHIP_VENDOR, CHIP_PRODUCT);

  // Steps following the oficial guide to setup MPSSE

  // Reset device (Resets includes readbuffer purge)
  CALL_FTDI(ftdi_usb_reset, "Unable to reset device");

  // Set chunk sizes to 64KiB
  const int chunkSize = 65536;

  CALL_FTDI(
    ftdi_write_data_set_chunksize, "Unable to set write chunk size", chunkSize);
  CALL_FTDI(
    ftdi_read_data_set_chunksize, "Unable to set read chunk size", chunkSize);

  // Set timeout that is used to flush remaining data from the receive buffer in
  // milliseconds.
  CALL_FTDI(ftdi_set_latency_timer, "Unable to set latency", 2);

  // Turn on flow control so no read requests are generated while buffer is full
  CALL_FTDI(ftdi_setflowctrl, "Unable to turn on flow control", SIO_RTS_CTS_HS);

  // Reset controller
  CALL_FTDI(ftdi_set_bitmode, "Unable to reset controller", 0x00, 0x00);
  // Enable MPSSE mode
  CALL_FTDI(ftdi_set_bitmode, "Unable to enable MPSSE mode", 0x00, 0x02);
}