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

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "hm05.hpp"

#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "optparse.h"

const int logBufferLength = 5000;
char logBuffer[logBufferLength];

void logMessage(int logLevel, const char *formatString, ...) {
  va_list args;
  va_start(args, formatString);
  vsnprintf(logBuffer, logBufferLength, formatString, args);
  va_end(args);
  fprintf(logLevel == LOG_ERROR ? stderr : stdout, "%s\n", logBuffer);
  fflush(stdout);
}

void usageMessage(void) {
  printf("Usage: hm05 <command> [<args>]\n"
         "\n"
         " hm05 read output-file         Read from cart to output-file\n"
         "\n"
         " hm05 write input-file         Write to cart input-file contents\n"
         "\n"
         " General options: \n"
         "  -h, --help                   Print this help message\n");
}

CartCommContext ccc;

int main(int argc, char *argv[]) {

  struct optparse_long longopts[] = {{"help", 'h', OPTPARSE_NONE},
                                     /*  {"brief", 'b', OPTPARSE_NONE},
                                       {"color", 'c', OPTPARSE_REQUIRED},
                                       {"delay", 'd', OPTPARSE_OPTIONAL},*/
                                     {0}};

  char mode = 0; // r: read, w: write

  if (argc < 2) {
    usageMessage();
    return 0;
  }

  if (strcmp(argv[1], "write") == 0) {
    mode = 'w';
  }
  if (strcmp(argv[1], "read") == 0) {
    mode = 'r';
  }

  if (!mode) {
    usageMessage();
    return 1;
  }

  struct optparse options;
  optparse_init(&options, argv + 1);

  int option;
  while ((option = optparse_long(&options, longopts, NULL)) != -1) {
    switch (option) {
      case 'h':
        usageMessage();
        return 0;
    }
  }

  // If filename was not passed
  if (options.optind + 1 >= argc) {
    usageMessage();
    return 0;
  }

  struct ftdi_context *ftdi = ftdi_new();

  if (ftdi == 0) {
    return 1;
  }

  if (openDeviceAndSetupMPSSE(ftdi, &ccc) < 0) {
    powerOff(&ccc);
    return 1;
  }

  FILE *f;
  const auto filename = argv[options.optind + 1];
  switch (mode) {
    case 'r':
      f = fopen(filename, "wb");
      if (!f) {
        logMessage(LOG_ERROR, "Cannot open file %s for writing", filename);
        powerOff(&ccc);
        return -1;
      }

      logMessage(LOG_INFO, "Reading ROM to %s", filename);
      readRom(&ccc);

      fwrite(ccc.romBuffer, 1, ROM_BUFFER_SIZE, f);
      fflush(f);
      fclose(f);
      break;
    case 'w':
      f = fopen(filename, "rb");
      if (!f) {
        logMessage(LOG_ERROR, "Cannot open file %s", filename);
        powerOff(&ccc);
        return -1;
      }
      fseek(f, 0, SEEK_END);
      const int romSize = ftell(f);
      fseek(f, 0L, SEEK_SET);
      fread(ccc.romBuffer, 1, romSize, f);
      fclose(f);

      logMessage(LOG_INFO, "Writting ROM to %s", filename);
      writeRom(&ccc, romSize);
      break;
  }

  powerOff(&ccc);
  return 0;
}

#ifdef IS_POSIX

void sleepMs(unsigned int ms) {
  unsigned int secs = ms / 1000;
  unsigned int nanoSecs = (ms - (secs * 1000)) * 1000 * 1000;
  timespec req = {secs, nanoSecs};
  nanosleep(&req, nullptr);
}

#endif