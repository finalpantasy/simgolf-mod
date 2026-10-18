#!/usr/bin/env python3
"""Compile the real CRT low-level save path with filesystem fault injection."""
import pathlib, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
crt = (ROOT/'port/src/port/crt.c').read_text()
body = crt.split('// ------------------------------------------------------------------ low-level io')[1].split('// ------------------------------------------------------------------ stdio')[0]
prelude = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <sys/wait.h>
typedef uintptr_t nu;
#define S(x) ((char *)(uintptr_t)(x))
#define P(x) ((void *)(uintptr_t)(x))
static int fail_lamp, fail_side, written, fail_replace_at, replace_calls, fail_persistently, crash_at;
static int sync_calls, fail_sync_at;
static int fs_resolve(const char *p,char *out,int n,int w) {(void)w;snprintf(out,n,"%s",p);return 1;}
static int fs_safe_saves(void) { return 1; }
static int fs_commit(int fd) { if(++sync_calls==fail_sync_at)return 0;return fsync(fd)==0; }
static int fs_replace(const char *a,const char *b) { ++replace_calls;if(replace_calls==fail_replace_at || (fail_persistently&&replace_calls>=fail_replace_at))return 0;int ok=rename(a,b)==0;if(replace_calls==crash_at)_exit(73);return ok; }
static void fs_written(const char *p) {(void)p;written++;}
static void fs_deleted(const char *p) {(void)p;}
static void put(const char *p,const char *s){FILE *f=fopen(p,"wb");assert(f);assert(fputs(s,f)>=0);assert(!fclose(f));}
static void eq(const char *p,const char *s){char b[100]={0};FILE *f=fopen(p,"rb");assert(f);assert(fread(b,1,sizeof b-1,f)==strlen(s));fclose(f);assert(!strcmp(b,s));}
static int side(const char *p,const char *suffix){char b[700];snprintf(b,sizeof b,"%s%s",p,suffix);put(b,"new-side");return 1;}
static int camera_tiles_written(int fd,const char *p){(void)fd;return fail_side==1?0:side(p,".camera-tiles");}
static int lamps_game_written(int fd,const char *p){(void)fd;return fail_lamp?0:side(p,".course-lights");}
static int path_styles_written(int fd,const char *p){(void)fd;return fail_side==3?0:side(p,".path-styles");}
static int living_written(int fd,const char *p){(void)fd;return fail_side==4?0:side(p,".living-club.json");}
static void living_closed(int fd){(void)fd;}
static void camera_tiles_closed(int fd){(void)fd;}
static void lamps_game_closed(int fd){(void)fd;}
static void path_styles_closed(int fd){(void)fd;}
'''
# The transaction implementation, once present, is compiled unchanged.
header = ROOT/'port/src/port/save_group.h'
if header.exists():
    assert 'O_WRONLY|O_CREAT|O_EXCL|O_BINARY' in header.read_text(), 'journal must use binary mode on Windows'
    prelude += '\n#include "port/save_group.h"\n'
main = r'''
static const char *suffixes[]={"", ".camera-tiles", ".course-lights", ".path-styles", ".living-club.json"};
static void old_group(void){char p[100];for(int i=0;i<5;i++){snprintf(p,sizeof p,"course.sve%s",suffixes[i]);put(p,i?"old-side":"old-save");}written=replace_calls=sync_calls=0;}
static void check_old(void){char p[100];for(int i=0;i<5;i++){snprintf(p,sizeof p,"course.sve%s",suffixes[i]);eq(p,i?"old-side":"old-save");}assert(!written);}
static int save(void){int fd=crt_open((nu)"course.sve",0x301);assert(fd>=0);assert(crt_write(fd,(nu)"new-save",8)==8);return crt_close(fd);}
int main(void){
 old_group();setenv("SIMGOLF_TEST_SAVE_FAIL_AFTER","3",1);
 int shortfd=crt_open((nu)"course.sve",0x301);assert(shortfd>=0);
 assert(crt_write(shortfd,(nu)"new-save",8)==3);assert(crt_close(shortfd)==-1);check_old();
 unsetenv("SIMGOLF_TEST_SAVE_FAIL_AFTER");
 old_group();fail_lamp=1;assert(save()==-1);check_old();fail_lamp=0;
 for(int i=1;i<=4;i++){if(i==2)continue;old_group();fail_side=i;assert(save()==-1);check_old();}fail_side=0;
 old_group();assert(save()==0);assert(written==1);eq("course.sve","new-save");int sync_total=sync_calls;
 // Every destination replacement can fail independently; the complete previous
 // generation is restored and no successful-save notification is emitted.
 for(int i=1;i<=5;i++){old_group();fail_replace_at=i;assert(save()==-1);check_old();}fail_replace_at=0;
 for(int i=1;i<=sync_total;i++){old_group();fail_sync_at=i;assert(save()==-1);check_old();}fail_sync_at=0;
 // A rollback I/O failure leaves its journal/backups for a later attempt.
 old_group();fail_replace_at=2;fail_persistently=1;assert(save()==-1);
 assert(access("course.sve.save-journal",F_OK)==0);
 assert(!save_group_recover("course.sve"));
 fail_persistently=0;fail_replace_at=0;assert(save_group_recover("course.sve"));check_old();
 // Simulate abrupt process death after EACH successful replacement, including
 // after the last sidecar but before journal deletion. Recovery is idempotent.
 for(int i=1;i<=5;i++){
   old_group();pid_t child=fork();assert(child>=0);
   if(!child){crash_at=i;save();_exit(74);}
   int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==73);
   int recovered=crt_open((nu)"course.sve",0);assert(recovered>=0);assert(crt_close(recovered)==0);
   assert(save_group_recover("course.sve"));check_old();
 }
 // Corrupt backups/metadata fail closed without replacing any live file.
 old_group();fail_replace_at=2;fail_persistently=1;assert(save()==-1);
 fail_persistently=0;fail_replace_at=0;put("course.sve.save-old-1","corrupt");
 assert(!save_group_recover("course.sve"));eq("course.sve","new-save");
 put("course.sve.save-old-1","old-side");assert(save_group_recover("course.sve"));check_old();
 put("course.sve.save-journal","truncated");assert(!save_group_recover("course.sve"));
 assert(crt_open((nu)"course.sve",0)==-1);assert(crt_open((nu)"course.sve",0x301)==-1);check_old();
 assert(remove("course.sve.save-journal")==0);
 // Existing companion without a staged replacement must block publication.
 old_group();put("course.sve.tmp","new-save");assert(save_group_clear_stage("course.sve"));
 assert(!save_group_commit("course.sve","course.sve.tmp"));check_old();
 // First save recovery removes newly introduced files instead of inventing old ones.
 for(int i=1;i<=5;i++){
   old_group();for(int j=0;j<5;j++){char p[100];snprintf(p,sizeof p,"course.sve%s",suffixes[j]);assert(!remove(p));}
   fail_replace_at=i;assert(save()==-1);
   for(int j=0;j<5;j++){char p[100];snprintf(p,sizeof p,"course.sve%s",suffixes[j]);assert(access(p,F_OK)!=0);}
 }fail_replace_at=0;
 puts("PASS: stage failures, all replace/sync failures, rollback retry, 5 process crashes, corrupt recovery, missing stage, first save");return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    p=pathlib.Path(td);(p/'test.c').write_text(prelude+body+main)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-Werror','-I'+str(ROOT/'port/src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],cwd=p,check=True)
