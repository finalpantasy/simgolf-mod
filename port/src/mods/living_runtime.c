// Native adapter. Mutation only at arrivals, emitted thoughts, round ends and
// complete save serialization. Reports, redraws and the bridge snapshot are read only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mods/living_club.h"
#include "mods/mods.h"
#include "port/roster.h"
#include "port/release_data.h"

#define SLOTS 152
#define G8(a) (*(int8_t *)(uintptr_t)(a))
#define G16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
static LcWorld world;
static LcContent content;
static int ready, loading, loaded, writing, live[SLOTS], ended[SLOTS], tee[SLOTS], last_on;
static struct { int event, arg, stamp; char text[LC_TEXT]; } utterance[SLOTS];

static int profile(int slot) { return slot>=0&&slot<SLOTS ? G16(0x57956eu+(unsigned)slot*0x100u):-1; }
static int traits(int id) { return roster_profile_valid(id) ? (unsigned char)G8(roster_address(0x4d6088u)+(unsigned)id*0x230u+0x20u)&31:0; }
static int hole(int slot) { return G8(0x5794d9u+(unsigned)slot*0x100u); }
static void name_of(int id,char out[32]) {
  *out=0;
  if(roster_profile_valid(id)) {
    const char *s=(const char *)(uintptr_t)(roster_address(0x4d6088u)+(unsigned)id*0x230u+0x10u);
    int i=0;while(i<16&&s[i]){unsigned char ch=(unsigned char)s[i];out[i]=(ch>=32&&ch<127)?(char)ch:'?';i++;}out[i]=0;
  }
}
static LcContext context(int slot,int event,char name[32],char partner_name[32]) {
  int id=profile(slot), other=G16(0x57955au+(unsigned)slot*0x100u);
  int partner=profile(other);name_of(id,name);name_of(partner,partner_name);
  LcContext x={id,partner,traits(id),hole(slot),event,name,partner_name};return x;
}
static int configured(void) {
  if (!mods_get_int("golfers.living",1) || !mods_get_int("mods.living_club",1)) return 0;
  if (mods_enabled("living_club")) return 1;
  // Updaters preserve player-owned load_order.toml. Existing stock Remastered
  // profiles gain the feature without replacing that file or custom profiles.
  return !strcmp(mods_profile(),"default") && mods_enabled("ui_golfers");
}
int living_enabled(void) { return ready && configured(); }
void living_reset(void) {
  lc_reset(&world,content.seed);
  for(int i=0;i<SLOTS;i++){live[i]=-1;ended[i]=0;tee[i]=-1;utterance[i].event=-1;utterance[i].text[0]=0;}
  loading=loaded=writing=0;
}
void living_init(const char *dir) {
  if(!configured())return;
  char path[1024],err[1200],release_dir[1024];snprintf(release_dir,sizeof release_dir,"%s/..",dir);
  release_data_path(path,sizeof path,release_dir,"living_club.json");
  const char *override=getenv("SIMGOLF_LIVING_CONTENT");if(override&&*override)snprintf(path,sizeof path,"%s",override);
  ready=lc_content_load(&content,path,err,sizeof err);
  living_reset(); last_on=living_enabled();
  fprintf(stderr,"living-club: %s\n",ready?"25 personalities, 12 persistent stories, 1158 authored lines":err);
}
void living_visit(int slot) {
  if(!living_enabled()||loading||slot<0||slot>=SLOTS)return;
  char name[32],other[32];LcContext x=context(slot,61,name,other);
  if(!roster_profile_valid(x.profile))return;
  int arrival=G16(0x579576u+(unsigned)slot*0x100u);
  if(live[slot]==x.profile && !ended[slot] && tee[slot]==arrival)return;
  tee[slot]=arrival;
  live[slot]=x.profile;ended[slot]=0;
  lc_visit(&world,&content,&x,utterance[slot].text,sizeof utterance[slot].text);utterance[slot].event=61;
  if(mods_get_int("golfers.log",0))fprintf(stderr,"living-club: visit profile %d number %d persona %s: %s\n",x.profile,world.people[x.profile].visits,lc_persona_name(&content,&world.people[x.profile]),utterance[slot].text);
}
void living_event(int slot,int event,int arg) {
  if(!living_enabled()||loading||slot<0||slot>=SLOTS||hole(slot)<1||hole(slot)>36)return;
  char name[32],other[32];LcContext x=context(slot,event,name,other);
  if(!roster_profile_valid(x.profile))return;
  // A legacy in-progress round gets one first observed visit; a valid load is attached below.
  if(live[slot]!=x.profile)living_visit(slot);
  if(event==61 && utterance[slot].event==61) {
    utterance[slot].arg=arg;
    utterance[slot].stamp=x.hole*11+G8(0x5794dau+(unsigned)slot*0x100u);
    return;
  }
  char line[LC_TEXT];lc_event(&world,&content,&x,line,sizeof line);
  if(!*line)return;
  utterance[slot].event=event;utterance[slot].arg=arg;
  utterance[slot].stamp=x.hole*11+G8(0x5794dau+(unsigned)slot*0x100u);
  snprintf(utterance[slot].text,sizeof utterance[slot].text,"%s",line);
  if(mods_get_int("golfers.log",0))fprintf(stderr,"living-club: event profile %d visit %d event %d hole %d: %s\n",x.profile,world.people[x.profile].visits,event,x.hole,line);
}
void living_frame(void) {
  int on=living_enabled();
  if(on!=last_on){for(int i=0;i<SLOTS;i++)utterance[i].event=-1;last_on=on;}
  if(!on||loading)return;
  for(int i=0;i<SLOTS;i++) {
    int h=hole(i),id=profile(i);
    if(loaded && h!=0 && h!=19 && roster_profile_valid(id)) {
      if(world.people[id].used){live[i]=id;tee[i]=G16(0x579576u+(unsigned)i*0x100u);}
      else living_visit(i);
    }
    if(live[i]<0||ended[i])continue;
    if(h==19||h==0||id!=live[i]) {
      int total=0,finished=0;
      for(int j=1;j<=18;j++){int strokes=G8(0x5794dbu+(unsigned)i*0x100u+(unsigned)j);if(strokes>0){total+=strokes;finished++;}}
      // Best-round comparisons only for a full original 18-hole card.
      lc_leave(&world,&content,live[i],total,h==19&&finished==18);
      ended[i]=1;live[i]=-1;
    }
  }
  loaded=0;
}
int living_thought(int slot,int event,int arg,int stamp,char *out,size_t n) {
  if(!living_enabled()||loading||slot<0||slot>=SLOTS||utterance[slot].event!=event||utterance[slot].arg!=arg||utterance[slot].stamp!=stamp||!*utterance[slot].text)return 0;
  snprintf(out,n,"%s",utterance[slot].text);return 1;
}
int living_reaction(int slot,int event,int original) {
  if(!living_enabled()||loading||slot<0||slot>=SLOTS||!mods_get_int("golfers.reactions",0))return original;
  int id=profile(slot);if(!roster_profile_valid(id)||!world.people[id].used)return original;
  return lc_reaction(&content,&world.people[id],event,original);
}
extern const char *crt_real_path(unsigned fd);
void living_save_field(int fd,int reading) {
  if(!living_enabled())return;
  if(!reading){writing=1;return;}
  living_reset();loading=1;
  const char *path=crt_real_path((unsigned)fd);char err[1200];
  int ok=path&&lc_load(&world,&content,path,err,sizeof err);
  fprintf(stderr,"living-club: %s\n",ok?"restored checksum-bound visit histories":path?err:"save path unavailable");
}
void living_save_end(void) { if(loading){loading=0;loaded=1;} }
void living_written(const char *path) {
  if(!living_enabled()||!writing)return;
  size_t len=strlen(path);if(len<4||_stricmp(path+len-4,".sve"))return;
  writing=0;char err[1200];
  int ok=lc_save(&world,&content,path,err,sizeof err);
  fprintf(stderr,"living-club: %s\n",ok?"saved checksum-bound visit histories":err);
}
void living_card(int slot,char *identity,size_t ni,char *story,size_t ns) {
  if(ni)*identity=0;if(ns)*story=0;
  if(!living_enabled()||slot<0||slot>=SLOTS)return;
  int id=profile(slot);if(!roster_profile_valid(id)||!world.people[id].used)return;
  const LcPerson *p=&world.people[id];
  snprintf(identity,ni,"%s | Visit %d | Confidence %d",lc_persona_name(&content,p),p->visits,p->confidence);
  lc_summary(&content,p,story,ns);
}
char *living_snapshot(void) {
  JsonVal *v=lc_snapshot(&world,&content);json_set(v,"enabled",json_bool(living_enabled()));
  char *s=json_write_compact(v);json_free(v);return s;
}
int living_diary(int slot,char lines[5][100]) {
  if(!living_enabled())return 0;
  int id=profile(slot);if(!roster_profile_valid(id)||!world.people[id].used)return 0;
  int n=0;
  for(int i=0;i<LC_JOURNAL;i++)if(*world.people[id].journal[i])
    snprintf(lines[n++],100,"%s",world.people[id].journal[i]);
  return n;
}
