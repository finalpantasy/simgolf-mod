// Additional identities use their own records. Original slot meanings stay fixed.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "port/roster.h"
#include "port/runtime.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

#ifdef ROSTER_TEST_IMAGE
void *roster_test_image(unsigned address);
#define IMAGE(address) roster_test_image((unsigned)(address))
#else
#define IMAGE(address) ((void *)(uintptr_t)(address))
#endif

typedef struct {
  const char *name, *profession, *bio;
  uint8_t look[9], flags, skills[10];
  const char *quotes[6];
  int celebrity_type;
} RosterSeed;
#include "../../../../data/roster/new_roster_data.h"

enum { PEOPLE = 560, MEMBER = 44, QUOTES = 1250, OLD = 84 };
static _Alignas(16) uint8_t people[ROSTER_CAPACITY*PEOPLE];
static _Alignas(16) uint8_t membership[ROSTER_CAPACITY*MEMBER];
static _Alignas(16) uint8_t quotes[ROSTER_CAPACITY*QUOTES];
static _Alignas(16) uint8_t pros[120*56];
static int initialized, pro_indices[10], celebrity_indices[10], indices_ready;

int roster_enabled(void) {
#ifndef __EMSCRIPTEN__
  return mods_get_int("gameplay.new_roster", 1) != 0;
#else
  return 1;
#endif
}
static void initialize(void) {
  if (initialized) return;
  memcpy(people,IMAGE(0x4d6088),OLD*PEOPLE);
  memcpy(membership,IMAGE(0x5849e0),OLD*MEMBER);
  memcpy(quotes,IMAGE(0x543d10),OLD*QUOTES);
  memcpy(pros,IMAGE(0x58dd50),100*56);
  for(int i=100;i<120;i++)pros[i*56+32]=255;
  initialized=1;
}
unsigned roster_address(unsigned address) {
  if (address>=0x4d6088 && address<0x4e1848) { initialize();return (unsigned)(uintptr_t)people+address-0x4d6088; }
  if (address>=0x5849e0 && address<0x585850) { initialize();return (unsigned)(uintptr_t)membership+address-0x5849e0; }
  if (address>=0x543d10 && address<0x55d738) { initialize();return (unsigned)(uintptr_t)quotes+address-0x543d10; }
  if (address>=0x58dd50 && address<0x58f330) { initialize();return (unsigned)(uintptr_t)pros+address-0x58dd50; }
  return address;
}
int roster_profile_valid(int profile) {
  return profile>=0 && (profile<OLD || (roster_enabled() && profile<ROSTER_CAPACITY));
}
// Legacy R/id/N markers mean identity and portrait share the same id.
// S/packed markers keep them independent in the same three padding bytes:
// high bits are identity+1, low six bits are portrait+1 (zero = stock face).
// This preserves existing saves and imports without changing trailer sizes.
static int marker_value(const uint8_t *p,int identity) {
  if(p[41]==0x52 && p[43]==0x4e && p[42]>=1 && p[42]<=ROSTER_ADDITIONS)return p[42]-1;
  if(p[41]==0x53) {
    unsigned packed=p[42]|((unsigned)p[43]<<8),who=packed>>6,face=packed&63;
    if(who<=ROSTER_ADDITIONS && face<=ROSTER_ADDITIONS)return (int)(identity?who:face)-1;
  }
  return -1;
}
int roster_character(int profile) {
  if(!roster_profile_valid(profile))return -1;
  initialize();return marker_value(people+profile*PEOPLE,0);
}
int roster_identity(int profile) {
  if(!roster_profile_valid(profile))return -1;
  initialize();return marker_value(people+profile*PEOPLE,1);
}
static void face_marker(uint8_t *p,int identity,int face) {
  if(identity==face && face>=0){p[41]=0x52;p[42]=(uint8_t)(face+1);p[43]=0x4e;}
  else if(identity<0 && face<0)p[41]=p[42]=p[43]=0;
  else {unsigned packed=((unsigned)(identity+1)<<6)|(unsigned)(face+1);p[41]=0x53;p[42]=(uint8_t)packed;p[43]=(uint8_t)(packed>>8);}
}
unsigned roster_regular_count(void) { return roster_enabled()?100:75; }
unsigned roster_pro_count(void) { return roster_enabled()?120:100; }
unsigned roster_next_regular(unsigned profile) {return roster_enabled() && profile==75?84:profile+1;}
unsigned roster_member_next(unsigned address) {
  initialize();unsigned offset=address-(unsigned)(uintptr_t)membership;
  return (unsigned)(uintptr_t)membership+roster_next_regular(offset/MEMBER)*MEMBER+offset%MEMBER;
}
unsigned roster_person_next(unsigned address) {
  initialize();unsigned offset=address-(unsigned)(uintptr_t)people;
  return (unsigned)(uintptr_t)people+roster_next_regular(offset/PEOPLE)*PEOPLE+offset%PEOPLE;
}
unsigned roster_member_end(unsigned field) {
  initialize();return (unsigned)(uintptr_t)membership+(roster_enabled()?109:76)*MEMBER+field;
}
unsigned roster_pick_regular(unsigned ordinal) {
  unsigned n=roster_regular_count();ordinal%=n;
  return ordinal<75?ordinal+1:ROSTER_FIRST+ordinal-75;
}
// The face catalog is separate from imported head slots. Choosing a painted
// face uses its stable marker without consuming or replacing a custom slot.
int roster_face_count(int male) {
  if(!roster_enabled() || (male!=0 && male!=1))return 0;
  int count=0;for(int i=0;i<ROSTER_ADDITIONS;i++)if(!(roster_seeds[i].look[1]&128)==male)count++;
  return count;
}
int roster_face_character(int male,int ordinal) {
  if(ordinal<0 || !roster_enabled() || (male!=0 && male!=1))return -1;
  for(int i=0;i<ROSTER_ADDITIONS;i++)if(!(roster_seeds[i].look[1]&128)==male && ordinal--==0)return i;
  return -1;
}
const char *roster_face_name(int character) {
  return character>=0 && character<ROSTER_ADDITIONS?roster_seeds[character].name:"";
}
int roster_face_apply(int profile,int male,int head,int character) {
  if(!roster_profile_valid(profile) || (male!=0 && male!=1))return 0;
  initialize();uint8_t *p=people+profile*PEOPLE;
  if(!(p[33]&128)!=male)return 0;
  int identity=marker_value(p,1);
  if(character>=0) {
    if(!roster_enabled() || character>=ROSTER_ADDITIONS)return 0;
    const RosterSeed *s=&roster_seeds[character];
    if(!(s->look[1]&128)!=male)return 0;
    p[34]=s->look[2];p[39]=s->look[7];p[40]=s->look[8];
    face_marker(p,identity,character);
  } else {
    unsigned count=*(unsigned *)IMAGE(0x59b76c+male*4);
    if(head<0 || head>=72 || (unsigned)head>=count)return 0;
    p[34]=(uint8_t)head;face_marker(p,identity,-1);
  }
  p[44]|=128;return 1;
}
int roster_character_rows(int existing) {
  if(!roster_enabled() || existing<0 || existing>100)return existing;
  int count=existing;
  for(int i=0;i<ROSTER_ADDITIONS && count<100;i++) {
    char *entry=(char *)(uintptr_t)(0x80b130+count*100);
    snprintf(entry,100,"New Roster\\%s.chr",roster_seeds[i].name);count++;
  }
  fprintf(stderr,"roster: character picker appended %d entries\n",count-existing);
  return count;
}
// showCharacterEditor (004385d0) loses its file count in the cached
// decompilation. The old leaf menu also copies into a 1024-byte stack buffer
// and holds only32 row accelerators. Use the existing modern popup with eight
// files per page, keeping paths in the file table and showing just names.
unsigned FUN_0046d6e0(int x,unsigned y,unsigned mask,int menu,unsigned disabled);
int roster_choose_character(int count) {
  if(count<=0 || count>100)return -1;
  int page=0;
  for(;;) {
    char *text=(char *)IMAGE(0x51a068);
    int first=page*8,rows=count-first;if(rows>8)rows=8;
    int used=snprintf(text,1024,"Pick a character (%d/%d)\n",page+1,(count+7)/8);
    for(int i=0;i<rows;i++) {
      const char *file=(const char *)(uintptr_t)(0x80b130+(first+i)*100);
      const char *label=file;for(const char *p=file;*p;p++)if(*p=='\\' || *p=='/')label=p+1;
      int length=(int)strlen(label);if(length>=4 && !strcmp(label+length-4,".chr"))length-=4;
      used+=snprintf(text+used,1024-used," %.*s\n",length,label);
    }
    int previous=-1,next=-1,items=rows;
    if(page>0){previous=items++;used+=snprintf(text+used,1024-used," Previous page\n");}
    if(first+rows<count){next=items++;snprintf(text+used,1024-used," Next page\n");}
    fprintf(stderr,"roster: character page %d first %d rows %d previous %d next %d\n",page,first,rows,previous,next);
    int choice=(int)FUN_0046d6e0(400,80,0,1,0);
    if(choice<0)return -1;
    if(choice<rows)return first+choice;
    if(choice==previous){page--;continue;}
    if(choice==next){page++;continue;}
    return -1;
  }
}
int roster_pro_rows(int existing) {
  if(existing<0 || existing>100)return existing;
  if(roster_enabled()) {
    int count=existing;
    for(int j=25;j<35 && count<100;j++) {
      char name[64];snprintf(name,sizeof name,"%s.pro",roster_seeds[j].name);
      int found=0;
      for(int i=0;i<count;i++)if(!strcmp((const char *)(uintptr_t)(0x80b130+i*100),name)){found=1;break;}
      if(!found)snprintf((char *)(uintptr_t)(0x80b130+count++*100),100,"%s",name);
    }
    return count;
  }
  int count=0;
  for(int i=0;i<existing;i++) {
    const char *entry=(const char *)(uintptr_t)(0x80b130+i*100);int authored=0;
    for(int j=25;j<35;j++) {
      char name[64];snprintf(name,sizeof name,"%s.pro",roster_seeds[j].name);
      if(!strcmp(entry,name)){authored=1;break;}
    }
    if(!authored){if(count!=i)memmove((void *)(uintptr_t)(0x80b130+count*100),entry,100);count++;}
  }
  return count;
}
int roster_import_allowed(const char *name) {
  if(!roster_enabled() || !name)return 1;
  const char *base=name;for(const char *p=name;*p;p++)if(*p=='/' || *p=='\\')base=p+1;
  for(int i=0;i<ROSTER_ADDITIONS;i++) {
    size_t n=strlen(roster_seeds[i].name);
    if(!strncmp(base,roster_seeds[i].name,n) && (!strcmp(base+n,".chr") || !strcmp(base+n,".pro"))) {
      int male=!(roster_seeds[i].look[1]&128);
      if(*(unsigned *)(uintptr_t)(0x59b76c+male*4)>=72) {
        fprintf(stderr,"roster: imported face bank is full; preserved current character and indices\n");return 0;
      }
      break;
    }
  }
  return 1;
}
void roster_spawned(unsigned slot) {
  if(slot>=0x98 || !roster_enabled())return;
#ifdef __EMSCRIPTEN__
  return;
#else
  if(!mods_get_int("gameplay.authored_visitor_skills",0))return;
#endif
  uint8_t *g=(uint8_t *)(uintptr_t)(0x5794b8+slot*256);
  int id=roster_identity(*(int16_t *)(g+0xb6));
  if(id>=0 && id<25)memcpy(g+0xf0,roster_seeds[id].skills,10);
}
static void copy_seed(int id,int profile) {
  const RosterSeed *s=&roster_seeds[id];uint8_t *p=people+profile*PEOPLE;
  memset(p,0,PEOPLE);snprintf((char *)p,16,"%s",s->profession);
  snprintf((char *)p+16,16,"%s",s->name);memcpy(p+32,s->look,9);
  p[0x29]=0x52;p[0x2a]=(uint8_t)(id+1);p[0x2b]=0x4e;p[0x2c]=s->flags;
  snprintf((char *)p+48,512,"%s",s->bio);
  static const int events[6]={0,1,3,9,13,19};
  uint8_t *q=quotes+profile*QUOTES;memset(q,0,QUOTES);
  for(int i=0;i<6;i++)snprintf((char *)q+events[i]*50,50,"%s",s->quotes[i]);
}
void roster_seed(int reset) {
  initialize();if(!roster_enabled())return;
  for(int i=0;i<ROSTER_ADDITIONS;i++) {
    int profile=ROSTER_FIRST+i;
    if(reset || roster_identity(profile)!=i) {
      copy_seed(i,profile);memset(membership+profile*MEMBER,0,MEMBER);
      // All new regulars are available to meet without replacing old members.
      if(i<25)membership[profile*MEMBER+2]=1;
    }
  }
  fprintf(stderr,"roster: ready 25 regulars 10 pros 10 fictional celebrities\n");
}
int roster_pro_index(int id) { return indices_ready && id>=25 && id<35?pro_indices[id-25]:-1; }
int roster_celebrity_index(int id) { return indices_ready && id>=35 && id<45?celebrity_indices[id-35]:-1; }
void roster_append_tables(void) {
  for(int i=0;i<10;i++)pro_indices[i]=celebrity_indices[i]=-1;
  indices_ready=1;if(!roster_enabled())return;
  for(int i=0;i<10;i++) {
    const RosterSeed *s=&roster_seeds[25+i];int index=-1;
    for(int j=0;j<120;j++)if(!strncmp((char *)pros+j*56,s->name,32)){index=j;break;}
    int existing=index>=0 && pros[index*56+32]!=255;
    for(int j=0;index<0 && j<120;j++) {
      uint8_t *p=pros+j*56;
      if(p[32]==255) {index=j;break;}
    }
    if(index>=0 && !existing) {
      uint8_t *p=pros+index*56;memset(p,0,56);
      snprintf((char *)p,32,"%s",s->name);p[32]=(s->look[3]>>4)|((s->look[1]&128)?4:0);
      p[33]=s->look[7];p[34]=s->look[3]&15;p[35]=s->look[4];p[36]=s->look[5];
      memcpy(p+37,s->skills,10);int sum=0;for(int k=0;k<10;k++)sum+=s->skills[k];
      p[54]=(uint8_t)sum;p[55]=(uint8_t)(sum>>8);
    }
    if(index>=0)pro_indices[i]=index;
    else fprintf(stderr,"roster: pro table full, cannot append %s\n",s->name);
    s=&roster_seeds[35+i];index=-1;
    for(int j=0;j<100;j++)if(!strncmp((char *)IMAGE(0x55d738+j*37),s->name,32)){index=j;break;}
    existing=index>=0 && *(uint8_t *)IMAGE(0x55d738+index*37+32)!=255;
    for(int j=0;index<0 && j<100;j++)if(*(uint8_t *)IMAGE(0x55d738+j*37+32)==255){index=j;break;}
    if(index>=0 && !existing) {
      uint8_t *p=(uint8_t *)IMAGE(0x55d738+index*37);memset(p,0,37);
      snprintf((char *)p,32,"%s",s->name);p[32]=(uint8_t)s->celebrity_type;
      p[33]=s->look[7];p[34]=s->look[8];p[35]=s->look[4];p[36]=s->look[5];
    }
    if(index>=0)celebrity_indices[i]=index;
    else fprintf(stderr,"roster: celebrity table full, cannot append %s\n",s->name);
  }
  fprintf(stderr,"roster: appended touring pros and celebrity guests\n");
}
void roster_bind_slot(int slot) {
  if(!roster_enabled() || slot<0 || slot>=0x98)return;
  uint8_t *g=(uint8_t *)(uintptr_t)(0x5794b8+slot*256);
  if((g[0x18]&0xe0)!=0x20 || (g[0x18]&31)==4)return;
  int profile=*(int16_t *)(g+0xb6);if(!roster_profile_valid(profile))return;
  if(profile==0)return; // The player keeps the character selected or imported.
  int id=-1;for(int i=0;i<10;i++)if(roster_pro_index(25+i)==g[0xbb]){id=25+i;break;}
  if(id>=0) {
    int own=ROSTER_FIRST+id;
    if(roster_identity(own)!=id)copy_seed(id,own);
    *(int16_t *)(g+0xb6)=(int16_t)own;
  }
}

// Extra records trail the legacy body. Old saves remain readable. The fixed
// legacy blocks keep their exact lengths and slot identities.
typedef struct { char magic[4]; uint32_t version,count,bytes,checksum; } RosterTag;
enum { PROFILE_EXTRA_BYTES=ROSTER_ADDITIONS*(PEOPLE+MEMBER+QUOTES), EXTRA_BYTES=PROFILE_EXTRA_BYTES+20*56 };
static uint8_t staged[EXTRA_BYTES];
int crt_read(uint32_t fd,uint32_t buf,uint32_t n);
int crt_write(uint32_t fd,uint32_t buf,uint32_t n);
int crt_lseek(uint32_t fd,uint32_t off,uint32_t whence);
static uint32_t checksum(const uint8_t *p,size_t n) {uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static int valid_extra(const uint8_t *data) {
  size_t a=ROSTER_ADDITIONS*PEOPLE,b=ROSTER_ADDITIONS*MEMBER;
  for(int i=0;i<ROSTER_ADDITIONS;i++) {
    const uint8_t *p=data+i*PEOPLE;
    if(p[41]==0x53){unsigned packed=p[42]|((unsigned)p[43]<<8);if((packed>>6)>ROSTER_ADDITIONS || (packed&63)>ROSTER_ADDITIONS)return 0;}
    if((p[0x29]==0x52 && p[0x2b]==0x4e && (p[0x2a]<1 || p[0x2a]>ROSTER_ADDITIONS)) || !memchr(p,0,16) || !memchr(p+16,0,16) || !memchr(p+48,0,512) || p[34]>=72 || p[36]>9 || p[37]>9 || p[39]>3 || p[40]>4)return 0;
    for(int j=0;j<25;j++)if(!memchr(data+a+b+i*QUOTES+j*50,0,50))return 0;
  }
  for(int i=0;i<20;i++) {
    const uint8_t *p=data+PROFILE_EXTRA_BYTES+i*56;
    if(p[32]!=255 && (p[32]>7 || !memchr(p,0,32)))return 0;
  }
  return 1;
}
int roster_preflight_file(void *file,long body_end,long file_end,int course) {
  if(course || file_end==body_end)return 1;
  FILE *f=file;RosterTag t;
  if(file_end-body_end!=(long)(sizeof t+sizeof staged) || fseek(f,body_end,SEEK_SET) || fread(&t,sizeof t,1,f)!=1 || memcmp(t.magic,"SGRP",4) || t.version!=1 || t.count!=ROSTER_ADDITIONS || t.bytes!=sizeof staged || fread(staged,sizeof staged,1,f)!=1 || checksum(staged,sizeof staged)!=t.checksum || !valid_extra(staged)) {
    fprintf(stderr,"roster: rejected malformed extension before any load mutation\n");return 0;
  }
  return 1;
}
void roster_save_tail(void) {
  initialize();int fd=*(int *)0x568d08,reading=*(int *)0x53e638;
  size_t a=ROSTER_ADDITIONS*PEOPLE,b=ROSTER_ADDITIONS*MEMBER,c=ROSTER_ADDITIONS*QUOTES;
  if(!reading) {
    if(!roster_enabled())return;
    memcpy(staged,people+OLD*PEOPLE,a);memcpy(staged+a,membership+OLD*MEMBER,b);memcpy(staged+a+b,quotes+OLD*QUOTES,c);
    memcpy(staged+a+b+c,pros+100*56,20*56);
    RosterTag t={{'S','G','R','P'},1,ROSTER_ADDITIONS,EXTRA_BYTES,checksum(staged,sizeof staged)};
    if(crt_write(fd,(unsigned)(uintptr_t)&t,sizeof t)!=(int)sizeof t || crt_write(fd,(unsigned)(uintptr_t)staged,sizeof staged)!=(int)sizeof staged)crt_io_failed(fd);
    else fprintf(stderr,"roster: saved SGRP 45 additional identities\n");
    return;
  }
  int pos=crt_lseek(fd,0,1),end=crt_lseek(fd,0,2);crt_lseek(fd,pos,0);
  if(end==pos) {roster_seed(1);roster_append_tables();return;}
  RosterTag t;
  if(end-pos<(int)sizeof t || crt_read(fd,(unsigned)(uintptr_t)&t,sizeof t)!=(int)sizeof t || memcmp(t.magic,"SGRP",4) || t.version!=1 || t.count!=ROSTER_ADDITIONS || t.bytes!=sizeof staged || end-pos!=(int)(sizeof t+sizeof staged) || crt_read(fd,(unsigned)(uintptr_t)staged,sizeof staged)!=(int)sizeof staged || checksum(staged,sizeof staged)!=t.checksum) {
    fprintf(stderr,"roster: rejected invalid SGRP extension\n");crt_io_failed(fd);return;
  }
  if(!valid_extra(staged)){crt_io_failed(fd);return;}
  memcpy(people+OLD*PEOPLE,staged,a);memcpy(membership+OLD*MEMBER,staged+a,b);memcpy(quotes+OLD*QUOTES,staged+a+b,c);
  memcpy(pros+100*56,staged+a+b+c,20*56);
  roster_append_tables();
  fprintf(stderr,"roster: loaded SGRP 45 additional identities\n");
}
