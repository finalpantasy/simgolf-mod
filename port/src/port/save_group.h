// Recoverable replacement of a native save and its CRC-bound companions.
// Journal/backups use private names; existing save/sidecar formats do not change.
// A process interrupted before journal deletion rolls back on the next open.
// Single writer only. This is NOT a cross-file atomic rename or a guarantee
// against filesystem/hardware power-loss reordering (directory fsync is absent).
#ifndef SIMGOLF_SAVE_GROUP_H
#define SIMGOLF_SAVE_GROUP_H
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

enum { SAVE_GROUP_COUNT = 5, SAVE_GROUP_PATH = 640 };
static const char *const save_group_suffix[SAVE_GROUP_COUNT] = {
  "", ".camera-tiles", ".course-lights", ".path-styles", ".living-club.json"
};
typedef struct {
  uint32_t magic, version, old_mask;
  uint32_t size[SAVE_GROUP_COUNT], crc[SAVE_GROUP_COUNT], checksum;
} SaveGroupJournal;
static inline uint32_t save_group_crc(uint32_t c, const void *data, size_t n) {
  const unsigned char *p = data; c = ~c;
  while (n--) { c ^= *p++; for (int k=0;k<8;k++) c=(c>>1)^(0xedb88320u&-(int32_t)(c&1)); }
  return ~c;
}
static inline int save_group_name(char out[SAVE_GROUP_PATH], const char *base, const char *suffix) {
  int n = snprintf(out,SAVE_GROUP_PATH,"%s%s",base,suffix);
  return n >= 0 && n < SAVE_GROUP_PATH;
}
static inline int save_group_remove(const char *path) { return remove(path)==0 || errno==ENOENT; }
// Returns -1 for unreadable, 0 for absent, 1 for a complete readable file.
static inline int save_group_digest(const char *path, uint32_t *size, uint32_t *crc) {
  FILE *f=fopen(path,"rb"); if(!f)return errno==ENOENT?0:-1;
  unsigned char b[8192];size_t n;*size=*crc=0;int ok=1;
  while((n=fread(b,1,sizeof b,f))) {
    if(n>UINT32_MAX-*size){ok=0;break;}*size+=(uint32_t)n;*crc=save_group_crc(*crc,b,n);
  }
  if(ferror(f))ok=0;
  if(fclose(f))ok=0;
  return ok?1:-1;
}
static inline int save_group_copy(const char *src,const char *dst) {
  FILE *in=fopen(src,"rb");if(!in)return 0;
  FILE *out=fopen(dst,"wb");if(!out){fclose(in);return 0;}
  unsigned char b[8192];size_t n;int ok=1;
  while((n=fread(b,1,sizeof b,in)))if(fwrite(b,1,n,out)!=n){ok=0;break;}
  if(ferror(in))ok=0;
  if(fclose(in))ok=0;
  if(fflush(out)||!fs_commit(fileno(out)))ok=0;
  if(fclose(out))ok=0;
  return ok;
}
static inline void save_group_backup_name(char out[SAVE_GROUP_PATH],const char *base,int i) {
  snprintf(out,SAVE_GROUP_PATH,"%s.save-old-%d",base,i);
}
static inline void save_group_cleanup(const char *base) {
  char p[SAVE_GROUP_PATH];for(int i=0;i<SAVE_GROUP_COUNT;i++){save_group_backup_name(p,base,i);remove(p);}
}
static inline int save_group_recover(const char *base) {
  char journal[SAVE_GROUP_PATH],backup[SAVE_GROUP_PATH],dst[SAVE_GROUP_PATH],tmp[SAVE_GROUP_PATH];
  if(strlen(base)>500 || !save_group_name(journal,base,".save-journal"))return 0;
  FILE *f=fopen(journal,"rb");if(!f)return errno==ENOENT;
  SaveGroupJournal j;int ok=fread(&j,sizeof j,1,f)==1;
  if(ok)ok=fgetc(f)==EOF&&!ferror(f);
  if(fclose(f))ok=0;
  if(!ok||j.magic!=0x5347534au||j.version!=1||j.old_mask>=1u<<SAVE_GROUP_COUNT||
     j.checksum!=save_group_crc(0,&j,sizeof j-sizeof j.checksum))return 0;
  // Validate ALL backups before restoring ANY file. Never adopt corrupt backups.
  for(int i=0;i<SAVE_GROUP_COUNT;i++)if(j.old_mask&(1u<<i)) {
    uint32_t size,crc;save_group_backup_name(backup,base,i);
    if(save_group_digest(backup,&size,&crc)!=1||size!=j.size[i]||crc!=j.crc[i])return 0;
  }
  for(int i=0;i<SAVE_GROUP_COUNT;i++) {
    save_group_name(dst,base,save_group_suffix[i]);
    if(j.old_mask&(1u<<i)) {
      save_group_backup_name(backup,base,i);save_group_name(tmp,base,".save-restore");
      if(!save_group_copy(backup,tmp)||!fs_replace(tmp,dst))return 0;
    } else if(!save_group_remove(dst))return 0;
  }
  // Keep the journal and untouched backups on every error; recovery is idempotent.
  if(!save_group_remove(journal))return 0;
  save_group_cleanup(base);return 1;
}
static inline int save_group_clear_stage(const char *base) {
  char stage[SAVE_GROUP_PATH],p[SAVE_GROUP_PATH];
  if(strlen(base)>500||!save_group_name(stage,base,".tmp"))return 0;
  for(int i=1;i<SAVE_GROUP_COUNT;i++) {
    save_group_name(p,stage,save_group_suffix[i]);if(!save_group_remove(p))return 0;
  }
  return 1;
}
static inline int save_group_commit(const char *base,const char *stage) {
  char dst[SAVE_GROUP_PATH],src[SAVE_GROUP_PATH],backup[SAVE_GROUP_PATH],journal[SAVE_GROUP_PATH];
  SaveGroupJournal j={0};unsigned staged=0;
  if(strlen(base)>500||strlen(stage)>510)return 0;
  save_group_name(journal,base,".save-journal");
  // Do not overwrite a recovery journal or its backups.
  FILE *pending=fopen(journal,"rb");if(pending){fclose(pending);return 0;}if(errno!=ENOENT)return 0;
  for(int i=0;i<SAVE_GROUP_COUNT;i++) {
    uint32_t size,crc;save_group_name(src,stage,save_group_suffix[i]);
    int present=save_group_digest(src,&size,&crc);if(present<0||(!i&&!present))return 0;
    if(present) {
      int fd=open(src,O_RDWR|O_BINARY);if(fd<0)return 0;
      int ok=fs_commit(fd);if(close(fd))ok=0;if(!ok)return 0;staged|=1u<<i;
    }
    save_group_name(dst,base,save_group_suffix[i]);
    present=save_group_digest(dst,&j.size[i],&j.crc[i]);if(present<0)return 0;
    // A known companion may never silently become stale when its writer skipped.
    if(present&&!(staged&(1u<<i)))return 0;
    if(present) {
      j.old_mask|=1u<<i;save_group_backup_name(backup,base,i);
      if(!save_group_copy(dst,backup))return 0;
      if(save_group_digest(backup,&size,&crc)!=1||size!=j.size[i]||crc!=j.crc[i])return 0;
    }
  }
  j.magic=0x5347534au;j.version=1;j.checksum=save_group_crc(0,&j,sizeof j-sizeof j.checksum);
  // Exclusive creation: a partial journal also blocks future opens for manual
  // recovery. No destination has changed until the complete journal is flushed.
  int fd=open(journal,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0666);if(fd<0)return 0;
  int ok=write(fd,&j,sizeof j)==sizeof j;if(ok)ok=fs_commit(fd);if(close(fd))ok=0;
  if(!ok){save_group_remove(journal);return 0;}
  for(int i=0;ok&&i<SAVE_GROUP_COUNT;i++)if(staged&(1u<<i)) {
    save_group_name(src,stage,save_group_suffix[i]);save_group_name(dst,base,save_group_suffix[i]);
    ok=fs_replace(src,dst);
  }
  if(ok)ok=save_group_remove(journal);
  if(!ok) { if(!save_group_recover(base))fprintf(stderr,"save: recovery pending for %s; do not remove its journal/backups\n",base);return 0; }
  save_group_cleanup(base);return 1;
}
#endif
