#ifndef COSMOPOLITAN_LIBC_NT_STRUCT_BITMAPINFO_H_
#define COSMOPOLITAN_LIBC_NT_STRUCT_BITMAPINFO_H_

#include "libc/nt/struct/bitmapinfoheader.h"
#include "libc/nt/struct/rgbquad.h"

struct NtBitmapInfo {
  struct NtBitmapInfoHeader bmiHeader;
  struct NtRgbQuad bmiColors[1];
};

#endif /* COSMOPOLITAN_LIBC_NT_STRUCT_BITMAPINFO_H_ */
