#include "hm05.hpp"
#include <cstdio>
#include <stdarg.h>

const int logBufferLength = 5000;
char logBuffer[logBufferLength];

void logMessage(int logLevel, const char *formatString, ...) {
  va_list args;
  va_start(args, formatString);
  vsnprintf(logBuffer, logBufferLength, formatString, args);
  va_end(args);
  fprintf(logLevel == LOG_ERROR ? stderr : stdout, "%s\n", logBuffer);
}

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