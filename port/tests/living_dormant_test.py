#!/usr/bin/env python3
"""Exercise actual Living Club adapter/persistence with its feature switched off."""
import pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td)
 (p/'windows.h').write_text('#include <strings.h>\n#define _stricmp strcasecmp\n')
 (p/'mods').mkdir();(p/'port').mkdir()
 (p/'mods/mods.h').write_text('int mods_get_int(const char *,int); int mods_enabled(const char *); const char *mods_profile(void);\n')
 (p/'port/roster.h').write_text('#include <stdint.h>\nint roster_profile_valid(int); uintptr_t roster_address(uintptr_t);\n')
 (p/'port/release_data.h').write_text('#include <stddef.h>\nvoid release_data_path(char *,size_t,const char *,const char *);\n')
 harness=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mods/living_club.h"
static int enabled;
int mods_get_int(const char *s,int d){(void)d;return strcmp(s,"golfers.living")?1:enabled;}
int mods_enabled(const char *s){(void)s;return 1;}
const char *mods_profile(void){return "default";}
int roster_profile_valid(int id){return id>=0&&id<LC_PROFILES;}
uintptr_t roster_address(uintptr_t p){return p;}
void release_data_path(char *p,size_t n,const char *d,const char *f){(void)d;snprintf(p,n,"%s",f);}
static const char *source="old.sve";
const char *crt_real_path(unsigned fd){(void)fd;return source;}
static void put(const char *p,const char *s){FILE *f=fopen(p,"wb");assert(f);fputs(s,f);fclose(f);}
int main(void){
 char err[1200];LcContent c;assert(lc_content_load(&c,"living_club.json",err,sizeof err));
 static LcWorld w,readback;lc_reset(&w,c.seed);LcPerson *person=lc_person(&w,&c,7,3);assert(person);person->visits=12;
 put("old.sve","old-save");assert(lc_save(&w,&c,"old.sve",err,sizeof err));
 enabled=0;living_init(".");assert(!living_enabled());living_save_field(3,1);living_save_end();
 put("new.sve.tmp","changed-save");living_save_field(4,0);
 LIVING_WRITE;
 assert(lc_load(&readback,&c,"new.sve.tmp",err,sizeof err));assert(readback.people[7].visits==12);
 // Re-enable and reload: the history is still bound to the changed save bytes.
 enabled=1;source="new.sve.tmp";living_save_field(3,1);living_save_end();assert(living_enabled());
 char *snap=living_snapshot();assert(snap);JsonVal *v=json_parse(snap,err,sizeof err);assert(v);JsonVal *people=json_get(v,"people");assert(people&&people->len==1);assert(json_get(people->items[0],"visits")->n==12);json_free(v);free(snap);
 // Corrupt bytes must still fail the existing strict CRC validation.
 put("new.sve.tmp","wrong-save");assert(!lc_load(&readback,&c,"new.sve.tmp",err,sizeof err));
 // Corrupt history is never silently rebound or overwritten, even while off.
 enabled=0;source="new.sve.tmp";living_save_field(3,1);living_save_end();
 living_save_field(4,0);put("blocked.sve.tmp","newer-save");assert(!living_written(4,"blocked.sve.tmp"));
 living_closed(4);assert(living_written(4,"blocked.sve.tmp"));
 // A feature-disabled legacy save creates no empty history.
 put("legacy.sve","legacy-save");source="legacy.sve";living_save_field(3,1);living_save_end();
 living_save_field(4,0);assert(living_written(4,"legacy.sve"));
 FILE *absent=fopen("legacy.sve.living-club.json","rb");assert(!absent);living_closed(4);
 // An active save followed by disabling WITHOUT reloading still preserves it.
 enabled=1;living_save_field(4,0);assert(living_written(4,"legacy.sve"));living_closed(4);
 enabled=0;put("legacy.sve","changed-legacy-save");living_save_field(4,0);
 assert(living_written(4,"legacy.sve"));assert(lc_load(&readback,&c,"legacy.sve",err,sizeof err));
 lc_content_free(&c);puts("PASS: dormant history, re-enable, CRC rejection, corrupt-history guard, legacy save, disable without reload");return 0;
}
'''
 header=(ROOT/'port/src/mods/living_club.h').read_text()
 harness=harness.replace('LIVING_WRITE','assert(living_written(4,"new.sve.tmp"));' if 'int living_written(int fd' in header else 'living_written("new.sve.tmp");')
 (p/'test.c').write_text('#include <stdlib.h>\n'+harness)
 source=ROOT/'data/living_club.json'
 (p/'living_club.json').write_bytes(source.read_bytes())
 subprocess.run(['cc','-std=gnu11','-I'+str(p),'-I'+str(ROOT/'port/src'),str(p/'test.c'),str(ROOT/'port/src/mods/living_runtime.c'),str(ROOT/'port/src/mods/living_club.c'),str(ROOT/'port/src/mods/json.c'),str(ROOT/'port/src/mods/semver.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
