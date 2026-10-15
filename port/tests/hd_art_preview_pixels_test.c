#include <stdint.h>
#include <stdio.h>
#include "port/hd_art_preview_pixels.h"
int main(void) {
  unsigned tested=0;
  if (hd_preview_straight(0x00ffffffu)!=0) return 1;
  if (hd_preview_straight(0xff123456u)!=0xff123456u) return 2;
  if (hd_preview_straight(0x80402010u)!=0x80804020u) return 3;
  if (hd_preview_straight(0x40804020u)!=0x40ffff80u) return 4;
  for (unsigned a=1;a<=255;a++) for (unsigned c=0;c<=a;c++) {
    uint32_t p=(a<<24)|(c<<16)|(c<<8)|c;
    uint32_t q=hd_preview_straight(p);
    unsigned r=(q>>16)&255u;
    if ((q>>24)!=a || ((q>>8)&255u)!=r || (q&255u)!=r || (r*a+127u)/255u!=c) return 5;
    tested++;
  }
  printf("PASS: HD preview straight-alpha contract, %u composite round trips and4boundary cases\n",tested);
  return 0;
}
