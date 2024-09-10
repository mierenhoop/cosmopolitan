#ifndef COSMOPOLITAN_LIBC_NT_STRUCT_BITMAPINFOHEADER_H_
#define COSMOPOLITAN_LIBC_NT_STRUCT_BITMAPINFOHEADER_H_

struct NtBitmapInfoHeader {
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
};

#endif /* COSMOPOLITAN_LIBC_NT_STRUCT_BITMAPINFOHEADER_H_ */
