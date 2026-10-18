/* Isolated POSIX-backed Win32 surface for updater transaction tests only. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
typedef unsigned long DWORD;
typedef intptr_t HANDLE;
#define MAX_PATH 260
#define FALSE 0
#define FILE_ATTRIBUTE_DIRECTORY 16
#define FILE_ATTRIBUTE_NORMAL 128
#define FILE_ATTRIBUTE_REPARSE_POINT 1024
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_NO_MORE_FILES 18
#define ERROR_ALREADY_EXISTS 183
#define SYNCHRONIZE 1
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED ((DWORD)-1)
#define INFINITE ((DWORD)-1)
#define STARTF_USESHOWWINDOW 1
#define SW_HIDE 0
#define CREATE_NO_WINDOW 1
#define _stricmp strcasecmp
static DWORD last_error;
static int fail_copy, copy_count, process_alive, terminated, wait_count, timeout_process, terminate_ok=1, wait_fail_once;
typedef struct {char cFileName[MAX_PATH];DWORD dwFileAttributes;} WIN32_FIND_DATAA;
typedef struct {int wYear,wMonth,wDay,wHour,wMinute,wSecond;} SYSTEMTIME;
typedef struct {unsigned int cb,dwFlags,wShowWindow;} STARTUPINFOA;
typedef struct {HANDLE hProcess,hThread;} PROCESS_INFORMATION;
static const char *native(const char *p,char *b) {strcpy(b,p);for(char *s=b;*s;s++)if(*s=='\\')*s='/';return b;}
static void SetLastError(DWORD e) {last_error=e;}
static DWORD GetLastError(void) {return last_error;}
static DWORD GetFileAttributesA(const char *p) {char b[1024];struct stat st;if(lstat(native(p,b),&st)) {last_error=errno==ENOENT?ERROR_FILE_NOT_FOUND:ERROR_ACCESS_DENIED;return INVALID_FILE_ATTRIBUTES;}return S_ISDIR(st.st_mode)?FILE_ATTRIBUTE_DIRECTORY:S_ISLNK(st.st_mode)?FILE_ATTRIBUTE_REPARSE_POINT:0;}
static int CreateDirectoryA(const char *p,void *v) {char b[1024];(void)v;int r=mkdir(native(p,b),0700);if(r)last_error=errno==EEXIST?ERROR_ALREADY_EXISTS:ERROR_ACCESS_DENIED;return !r;}
static int SetFileAttributesA(const char *p,DWORD a) {(void)p;(void)a;return 1;}
static int DeleteFileA(const char *p) {char b[1024];return !unlink(native(p,b));}
static int RemoveDirectoryA(const char *p) {char b[1024];return !rmdir(native(p,b));}
static int MoveFileExA(const char *a,const char *b,int flags) {char x[1024],y[1024];(void)flags;return !rename(native(a,x),native(b,y));}
static int CopyFileA(const char *a,const char *b,int flags) {char x[1024],y[1024];(void)flags;copy_count++;if(fail_copy==copy_count){last_error=ERROR_ACCESS_DENIED;return 0;}FILE *in=fopen(native(a,x),"rb");if(!in)return 0;FILE *out=fopen(native(b,y),"wb");if(!out){fclose(in);return 0;}int c;while((c=fgetc(in))!=EOF)fputc(c,out);fclose(in);return !fclose(out);}
static int FindNextFileA(HANDLE h,WIN32_FIND_DATAA *fd) {struct dirent *e=readdir((DIR*)h);if(!e){last_error=ERROR_NO_MORE_FILES;return 0;}snprintf(fd->cFileName,sizeof fd->cFileName,"%s",e->d_name);return 1;}
static HANDLE FindFirstFileA(const char *p,WIN32_FIND_DATAA *fd) {char b[1024];native(p,b);char *slash=strrchr(b,'/');assert(slash);*slash=0;DIR *d=opendir(b);if(!d){last_error=ERROR_PATH_NOT_FOUND;return INVALID_HANDLE_VALUE;}if(!FindNextFileA((HANDLE)d,fd)){closedir(d);last_error=ERROR_FILE_NOT_FOUND;return INVALID_HANDLE_VALUE;}return (HANDLE)d;}
static int FindClose(HANDLE h) {return !closedir((DIR*)h);}
static void GetLocalTime(SYSTEMTIME *s) {*s=(SYSTEMTIME){2026,10,18,16,0,0};}
static HANDLE OpenProcess(DWORD d,int i,DWORD p) {(void)d;(void)i;(void)p;return 0;}
static int CloseHandle(HANDLE h) {(void)h;assert(!process_alive);return 1;}
static int SetEnvironmentVariableA(const char *a,const char *b) {(void)a;(void)b;return 1;}
static int CreateProcessA(void *a,char *b,void *c,void *d,int e,DWORD f,void *g,const char *h,STARTUPINFOA *i,PROCESS_INFORMATION *j) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;j->hProcess=1;j->hThread=2;process_alive=1;return 1;}
static DWORD WaitForSingleObject(HANDLE h,DWORD ms) {(void)h;wait_count++;if(wait_fail_once){wait_fail_once=0;return WAIT_FAILED;}if(timeout_process&&ms!=INFINITE)return WAIT_TIMEOUT;process_alive=0;return WAIT_OBJECT_0;}
static int TerminateProcess(HANDLE h,unsigned int code) {(void)h;(void)code;terminated++;return terminate_ok;}
static int GetExitCodeProcess(HANDLE h,DWORD *code) {(void)h;assert(!process_alive);*code=0;return 1;}
static void Sleep(DWORD ms) {(void)ms;}
static unsigned int GetSystemDirectoryA(char *s,unsigned int n) {snprintf(s,n,"system");return 6;}
static void logf_(const char *fmt,...) {(void)fmt;}
static FILE *win_fopen(const char *p,const char *m) {char b[1024];return fopen(native(p,b),m);}
#define fopen win_fopen
static char g_log_path[MAX_PATH];
static int program_sha256(const char *p,char out[65]) {char b[1024];struct stat s;if(stat(native(p,b),&s)||!s.st_size)return 0;strcpy(out,"verified");return 1;}
static int read_expected(const char *p,char out[65]) {(void)p;strcpy(out,"verified");return 1;}
static int sha256_file(const char *p,char out[65]) {(void)p;strcpy(out,"verified");return 1;}
static void write_file(const char *p,const char *text) {FILE *f=fopen(p,"wb");assert(f);fputs(text,f);fclose(f);}
static void expect_file(const char *p,const char *text) {char b[80]={0};FILE *f=fopen(p,"rb");assert(f);fread(b,1,sizeof b-1,f);fclose(f);assert(!strcmp(b,text));}
