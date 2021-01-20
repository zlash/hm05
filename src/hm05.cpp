#include "hm05.hpp"

int main(void) {
  struct ftdi_context *ftdi = ftdi_new();

  if (ftdi == 0) {
    return -1;
  }

  if (openDeviceAndSetupMPSSE(ftdi) < 0) {
    return -1;
  }

  return 0;
}