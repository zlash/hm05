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

  struct ftdi_context *ftdi = ftdi_new();

  if (ftdi == 0) {
    return 1;
  }

  if (openDeviceAndSetupMPSSE(ftdi, &ccc) < 0) {
    powerOff(&ccc);
    return 1;
  }

  readRom(&ccc);
  powerOff(&ccc);

  FILE *f = fopen("/tmp/000_zlash/rom.min", "wb");
  fwrite(ccc.romBuffer, 1, ROM_BUFFER_SIZE, f);
  fflush(f);
  fclose(f);

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