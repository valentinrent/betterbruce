#ifndef __SERIAL_HELPERS_H__
#define __SERIAL_HELPERS_H__
#ifndef LITE_VERSION
#include <precompiler_flags.h>

#include <Arduino.h>
#include <PSRamFS.h>

bool _setupPsramFs();
char *_readFileFromSerial(size_t fileSizeChar = 1024); // SAFE_STACK_BUFFER_SIZE
uint8_t *_readBytesFromSerial(size_t fileSize, size_t *outSize);

class FS;
bool getFsStorageFromPath(FS *&fs, String &filepath);

#endif
#endif
