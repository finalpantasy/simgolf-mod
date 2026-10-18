#!/usr/bin/env python3
"""Compile actual updater functions with isolated Win32 test doubles (not Windows QA)."""
import pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
SG = ROOT / 'port/src/updater/sgupdate.c'
UC = ROOT / 'port/src/port/update_check.c'
def function(text, name):
    # Locate the declaration containing this exact function, avoiding previous declarations.
    import re
    start = list(re.finditer(r'static [^\n]*\b' + name + r'\(', text))[0].start()
    brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]
def run_c(body):
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d); (p/'test.c').write_text(body)
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-format-truncation','-Wno-unused-function','-Wno-unused-variable','-Wno-missing-field-initializers',str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True,cwd=d)
PRE = r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef unsigned long DWORD;
typedef int HANDLE;
#define MAX_PATH 260
#define FALSE 0
#define FILE_ATTRIBUTE_DIRECTORY 16
#define FILE_ATTRIBUTE_REPARSE_POINT 1024
#define INVALID_HANDLE_VALUE -1
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_NO_MORE_FILES 18
#define ERROR_ACCESS_DENIED 5
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_SUCCESS 0
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
typedef struct { char cFileName[MAX_PATH]; DWORD dwFileAttributes; } WIN32_FIND_DATAA;
static DWORD error, first_error, next_error;
static int files, copies, destination_missing;
static void SetLastError(DWORD e) { error=e; }
static DWORD GetLastError(void) { return error; }
static DWORD GetFileAttributesA(const char *p) { if(destination_missing && !strcmp(p,"dst")){error=ERROR_ACCESS_DENIED;return INVALID_FILE_ATTRIBUTES;} return strstr(p,"child") ? 0 : FILE_ATTRIBUTE_DIRECTORY; }
static int CreateDirectoryA(const char *p, void *v) { (void)p;(void)v;return 1; }
static int CopyFileA(const char *a,const char *b,int c) { (void)a;(void)b;(void)c;copies++;return 1; }
static HANDLE FindFirstFileA(const char *p,WIN32_FIND_DATAA *fd) { (void)p; if(first_error) {error=first_error;return -1;} strcpy(fd->cFileName,files?"child":".");return 1; }
static int FindNextFileA(HANDLE h,WIN32_FIND_DATAA *fd) { (void)h;(void)fd;error=next_error;return 0; }
static int FindClose(HANDLE h) { (void)h;error=0;return 1; }
'''
class UpdaterSafety(unittest.TestCase):
    def test_copy_enumeration_errors(self):
        src = SG.read_text()
        funcs = '\n'.join(function(src,n) for n in ['path_attributes','mkdirs','mkparent','copy_tree'])
        run_c(PRE + funcs + r'''
int main(void) {
 first_error=ERROR_ACCESS_DENIED; assert(!copy_tree("src","dst")); assert(GetLastError()==ERROR_ACCESS_DENIED);
 first_error=ERROR_PATH_NOT_FOUND; assert(!copy_tree("src","dst"));
 first_error=ERROR_FILE_NOT_FOUND; assert(copy_tree("src","dst"));
 first_error=0; next_error=ERROR_ACCESS_DENIED; files=1; assert(!copy_tree("src","dst")); assert(copies==1);
 next_error=ERROR_NO_MORE_FILES; assert(copy_tree("src","dst"));
 destination_missing=1; first_error=ERROR_FILE_NOT_FOUND; assert(!copy_tree("src","dst"));
 return 0;
}
''')
    def test_apply_preserves_ui_and_rolls_back(self):
        src=SG.read_text()
        items=src[src.index('static const char PLAYER_EXE'):src.index('static char g_log_path')]
        funcs='\n'.join(function(src,n) for n in ['path_attributes','mkdirs','mkparent','rm_tree','copy_tree','winpath','do_apply'])
        run_c((ROOT/'tests/updater_win32_stubs.h').read_text()+items+funcs+r'''
int main(void) {
 mkdir("app",0700);mkdir("stage",0700);mkdir("app/ui",0700);mkdir("app/ui/terrain",0700);mkdir("stage/ui",0700);
 write_file("stage/Collect-Diagnostics.ps1","exporter");write_file("stage/DIAGNOSTICS.txt","guide");
 write_file("app/golf.exe","old"); write_file("app/Sim Golf Remastered.exe","old");
 write_file("stage/golf.exe","new");write_file("stage/ui/shipped.txt","new-ui");
 write_file("app/ui/shipped.txt","old-ui");write_file("app/ui/player.txt","custom");write_file("app/ui/terrain/index.tsv","terrain");
 assert(do_apply("app","stage",0,0,0)==0);
 expect_file("app/golf.exe","new");expect_file("app/Sim Golf Remastered.exe","new");expect_file("app/ui/shipped.txt","new-ui");
 expect_file("app/ui/player.txt","custom");expect_file("app/ui/terrain/index.tsv","terrain");
 /* Fail during UI preservation, after both program copies. */
 write_file("stage/golf.exe","next");copy_count=0;fail_copy=4;
 assert(do_apply("app","stage",0,0,0)!=0);
 expect_file("app/golf.exe","new");expect_file("app/Sim Golf Remastered.exe","new");expect_file("app/ui/shipped.txt","new-ui");
 expect_file("app/ui/player.txt","custom");expect_file("app/ui/terrain/index.tsv","terrain");
 /* Fail on the release overlay after all three preserved UI files. */
 copy_count=0;fail_copy=6;
 assert(do_apply("app","stage",0,0,0)!=0);
 expect_file("app/golf.exe","new");expect_file("app/ui/shipped.txt","new-ui");
 expect_file("app/ui/player.txt","custom");expect_file("app/ui/terrain/index.tsv","terrain");
 /* Player links must never be followed or silently dropped by preservation. */
 fail_copy=0;mkdir("outside",0700);write_file("outside/keep","safe");
 char target[1024];assert(getcwd(target,sizeof target));strcat(target,"/outside");
 assert(!symlink(target,"app/ui/link"));
 assert(do_apply("app","stage",0,0,0)!=0);
 expect_file("outside/keep","safe");expect_file("app/golf.exe","new");
 assert(GetFileAttributesA("app/ui/link") & FILE_ATTRIBUTE_REPARSE_POINT);
 assert(!unlink("app/ui/link"));
 /* Two more successful updates prune old backups without losing optional UI. */
 assert(do_apply("app","stage",0,0,0)==0);
 expect_file("app/golf.exe","next");expect_file("app/ui/terrain/index.tsv","terrain");
 write_file("stage/golf.exe","third");write_file("stage/ui/shipped.txt","third-ui");
 assert(do_apply("app","stage",0,0,0)==0);
 expect_file("app/Collect-Diagnostics.ps1","exporter");expect_file("app/DIAGNOSTICS.txt","guide");
 expect_file("app/golf.exe","third");expect_file("app/ui/shipped.txt","third-ui");
 expect_file("app/ui/player.txt","custom");expect_file("app/ui/terrain/index.tsv","terrain");
 write_file("stage/Collect-Diagnostics.ps1","new-exporter");write_file("stage/DIAGNOSTICS.txt","new-guide");
 assert(do_apply("app","stage",0,0,5)!=0);
 expect_file("app/Collect-Diagnostics.ps1","exporter");expect_file("app/DIAGNOSTICS.txt","guide");
 expect_file("app/ui/terrain/index.tsv","terrain");
 return 0;
}
''')
    def test_process_timeout_waits_for_exit(self):
        src=SG.read_text()
        funcs='\n'.join(function(src,n) for n in ['path_attributes','mkdirs','rm_tree','do_prepare'])
        run_c((ROOT/'tests/updater_win32_stubs.h').read_text()+funcs+function(UC.read_text(),'run_wait')+r'''
int main(void) {
 timeout_process=1;assert(do_prepare("zip","sha","stage")==3);assert(terminated==1);assert(wait_count==2);assert(!process_alive);
 terminated=wait_count=0;terminate_ok=0;wait_fail_once=1;
 assert(do_prepare("zip","sha","stage")==3);assert(terminated==1);assert(wait_count==2);assert(!process_alive);
 terminated=wait_count=0;wait_fail_once=1;char cmd[]="prepare";
 assert(run_wait(cmd)==0);assert(wait_count==2);assert(!terminated);assert(!process_alive);
 return 0;
}
''')
    def test_ui_keeps_player_assets(self):
        src=SG.read_text()
        # Whole-directory replacement must seed from the backup before release overlay.
        self.assertIn('copy_tree(bak, dst)', function(src,'do_apply'))
    def test_prepare_reaps_extractor(self):
        src=function(SG.read_text(),'do_prepare')
        self.assertIn('TerminateProcess(pi.hProcess',src)
        self.assertIn('INFINITE',src)
    def test_outer_wait_cannot_abandon_prepare(self):
        src=function(UC.read_text(),'run_wait')
        self.assertIn('INFINITE',src)
        self.assertNotIn('pi.hProcess, ms',src)
if __name__ == '__main__': unittest.main()
