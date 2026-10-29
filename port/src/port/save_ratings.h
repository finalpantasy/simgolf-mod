// Read-only metadata from the common save-field prefix. Never loads game state.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct { int32_t fun;int64_t skill; } SaveRatings;
static int save_ratings_compare(const SaveRatings *a,int av,const SaveRatings *b,int bv,int mode){
  if(mode!=1&&mode!=2)return 0;
  if(av!=bv)return av?-1:1;
  if(!av)return 0;
  int64_t x=mode==1?a->fun:a->skill,y=mode==1?b->fun:b->skill;
  return x==y?0:x>y?-1:1;
}
int file_save_sort_mode(void);
int file_save_rating(const char *name,SaveRatings *out);
static uint32_t save_ratings_u32(const unsigned char *p){
  return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t save_ratings_crc(const unsigned char *p,size_t n){
  uint32_t crc=~0u;
  while(n--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
  return ~crc;
}
static int save_ratings_parse(const unsigned char *p,size_t n,SaveRatings *out){
  if(!p||!out||n<137)return 0;
  size_t off=100; // The original save-name prefix, before FUN_0040afa0 fields.
  if(!memcmp(p+off,"SGMP",4)){
    uint32_t w=save_ratings_u32(p+off+8),h=save_ratings_u32(p+off+12);
    if(save_ratings_u32(p+off+4)!=1||w<50||h<50||w>160||h>160)return 0;
    off+=16;
  }
  if(n-off<37)return 0;
  if(!memcmp(p+off,"SGST",4)){
    if(save_ratings_u32(p+off+4)!=1||save_ratings_u32(p+off+8)!=64||
       save_ratings_u32(p+off+12)!=76||save_ratings_u32(p+off+16)!=64*76)return 0;
    if(n-off<24+64*76+37)return 0;
    if(save_ratings_crc(p+off+24,64*76)!=save_ratings_u32(p+off+20))return 0;
    off+=24+64*76;
  }
  // Common field offsets: fun 0x59ae78, then length/accuracy/imagination.
  out->fun=(int32_t)save_ratings_u32(p+off+17);
  out->skill=(int64_t)(int32_t)save_ratings_u32(p+off+25)+
             (int32_t)save_ratings_u32(p+off+29)+(int32_t)save_ratings_u32(p+off+33);
  return 1;
}
