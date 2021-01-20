#ifndef HM05_HPP
#define HM05_HPP

#include <ftdi.h>
#include <cassert>

#define VERSION_STRING "v0.0.1"

#define LOG_INFO  1
#define LOG_ERROR 2

int openDeviceAndSetupMPSSE(struct ftdi_context *ftdi);

#endif
