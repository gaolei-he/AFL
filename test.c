#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(void) {
  unsigned char buf[16] = {0};
  size_t n = fread(buf, 1, sizeof(buf), stdin);
  if (n < 3) return 0;

  if (buf[0]=='A' && buf[1]=='F' && buf[2]=='L') puts("hit1");

  if (n >= 6 && memcmp(buf, "AFL++", 5)==0 && buf[5]==0x7f) puts("hit2");

  if (n >= 8) {
    uint16_t x; memcpy(&x, &buf[6], 2);
    if (x == 0x1234) puts("hit3");
  }
  if (n >= 12) {
    uint32_t y; memcpy(&y, &buf[8], 4);
    if (y == 0xCAFEBABE) puts("hit4");
  }

  if (n >= 14 && buf[12]=='X' && buf[13]=='Y') puts("hit5");

  return 0;
}
