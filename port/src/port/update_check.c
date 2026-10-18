// The update check and the one-click update (simgolf mod, card task-80), see update_check.h.
// Native build only. Every network call runs on a worker thread with short time-outs; a failure is logged
// and the menu never notices. The swap itself is sgupdate.exe (a separate program: the game cannot replace
// its own exe while it runs).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "mods/mods.h"
#include "port/update_check.h"

#pragma comment(lib, "winhttp.lib")

#ifndef SIMGOLF_VERSION
#define SIMGOLF_VERSION "dev"
#endif

#define FEED_URL "https://api.github.com/repos/finalpantasy/simgolf-mod/releases/latest"
#define CHECK_TIMEOUT_MS 4000
#define DOWNLOAD_TIMEOUT_MS 15000
#define DOWNLOAD_TOTAL_MS (10 * 60 * 1000)

static UpdState *g_u;

static void wide(const char *s, wchar_t *out, int n) { MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n); }

static void download_progress(UpdState *u, uint64_t received, uint64_t total) {
  if (!u) return;
  int before=u->download_per_mille;
  u->download_kb=(int)(received/1000);
  u->download_total_kb=(int)(total/1000);
  int amount=total?(int)(received>=total?1000:received*1000/total):-1;
  InterlockedExchange((LONG *)&u->download_per_mille,amount);
  if(amount>=0 && before/100!=amount/100)
    fprintf(stderr,"update: download %d%% (%llu/%llu bytes)\n",amount/10,(unsigned long long)received,(unsigned long long)total);
}

// Reads url (http, https, file:, or a plain path for tests) into memory (mem) or into the file dest.
// Returns 1 on success. mem is malloc'd (NUL-terminated) and capped at max bytes.
static int fetch_progress(const char *url, char **mem, size_t *mem_len, size_t max, const char *dest, int timeout_ms, UpdState *progress) {
  if (mem) *mem = NULL;
  if (mem_len) *mem_len = 0;
  FILE *out = NULL;
  size_t cap = 0, len = 0;
  char *buf = NULL;
  int ok = 0;
  uint64_t received=0,total=0;
  if (dest && !(out = fopen(dest, "wb"))) return 0;
  if (_strnicmp(url, "http://", 7) && _strnicmp(url, "https://", 8)) {   // a local file (tests)
    if (!_strnicmp(url, "file:///", 8)) url += 8;
    FILE *in = fopen(url, "rb");
    if (in) {
      if (_fseeki64(in,0,SEEK_END)==0) { __int64 end=_ftelli64(in); if(end>0)total=(uint64_t)end; }
      rewind(in);download_progress(progress,0,total);
      char tmp[65536];
      size_t n;
      ok = 1;
      while ((n = fread(tmp, 1, sizeof tmp, in)) > 0) {
        if (out) {
          if (fwrite(tmp, 1, n, out) != n) {ok = 0;break;}
          received+=n;download_progress(progress,received,total);
        } else {
          if (len + n > max) { ok = 0; break; }
          buf = (char *)realloc(buf, len + n + 1);
          memcpy(buf + len, tmp, n);
          len += n;
        }
      }
      if(ferror(in))ok=0;
      if(fclose(in)!=0)ok=0;
    }
    goto done;
  }
  {
    wchar_t wurl[1100];
    wide(url, wurl, 1100);
    URL_COMPONENTS uc = {sizeof uc};
    wchar_t host[256], path[1024];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) goto done;
    HINTERNET ses = WinHttpOpen(L"SimGolfRemastered/" L"1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) goto done;
    WinHttpSetTimeouts(ses, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                        : NULL;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL)) {
      DWORD code = 0, sz = sizeof code;
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
      DWORD t0 = GetTickCount();
      if (code == 200) {
        wchar_t length[64]={0}; DWORD length_size=sizeof length;
        if(WinHttpQueryHeaders(req,WINHTTP_QUERY_CONTENT_LENGTH,NULL,length,&length_size,NULL))total=_wcstoui64(length,NULL,10);
        download_progress(progress,0,total);
        ok = 1;
        for (;;) {
          DWORD avail = 0, got = 0;
          char tmp[65536];
          if (!WinHttpQueryDataAvailable(req, &avail)) { ok = 0; break; }
          if (!avail) break;
          if (avail > sizeof tmp) avail = sizeof tmp;
          if (!WinHttpReadData(req, tmp, avail, &got) || !got) { ok = 0; break; }
          if (out) {
            if (fwrite(tmp, 1, got, out) != got) { ok = 0; break; }
            received+=got;download_progress(progress,received,total);
          } else {
            if (len + got > max) { ok = 0; break; }
            buf = (char *)realloc(buf, len + got + 1);
            memcpy(buf + len, tmp, got);
            len += got;
          }
          if (GetTickCount() - t0 > DOWNLOAD_TOTAL_MS) { ok = 0; break; }
        }
        if (total && (out ? received : len) != total) ok = 0;
      } else fprintf(stderr, "update: %s answered %lu\n", url, (unsigned long)code);
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
  }
done:
  if (out && fclose(out)!=0) ok=0;
  if (!ok) {
    free(buf);
    if (dest) remove(dest);
    return 0;
  }
  if (buf) buf[len] = 0;
  if (mem) *mem = buf;
  else free(buf);
  if (mem_len) *mem_len = len;
  return 1;
}

static int fetch(const char *url,char **mem,size_t *mem_len,size_t max,const char *dest,int timeout_ms) {
  return fetch_progress(url,mem,mem_len,max,dest,timeout_ms,NULL);
}


static void app_dir(char *out, size_t n) {
  GetModuleFileNameA(NULL, out, (DWORD)n);
  char *s = strrchr(out, '\\');
  if (s) *s = 0;
}

// ------------------------------------------------------------------ the check

static DWORD WINAPI check_thread(LPVOID arg) {
  (void)arg;
  const char *feed = getenv("SIMGOLF_UPDATE_FEED");
  if (!feed || !*feed) feed = FEED_URL;
  fprintf(stderr, "update: request %s\n", feed);
  char *body = NULL;
  size_t n = 0;
  if (!fetch(feed, &body, &n, 1 << 20, NULL, CHECK_TIMEOUT_MS)) {
    fprintf(stderr, "update: check failed (offline or no answer), staying silent\n");
    return 0;
  }
  char ver[16], zip[512], sha[512];
  UpdState *u = g_u;
  if (!upd_parse_feed(body, ver, sizeof ver, zip, sizeof zip, sha, sizeof sha)) {
    fprintf(stderr, "update: the answer is not a release with a zip and a checksum\n");
    free(body);
    return 0;
  }
  free(body);
  const char *later = mods_get("updates.later");
  int newer = upd_version_cmp(SIMGOLF_VERSION, ver) > 0;
  int hidden = later && *later && !upd_version_cmp(later, ver);
  fprintf(stderr, "update: newest %s, this build %s, %s\n", ver, SIMGOLF_VERSION,
          !newer ? "up to date" : hidden ? "newer but the player pressed Later for it" : "notice on");
  if (newer && !hidden) {
    snprintf(u->latest, sizeof u->latest, "%s", ver);
    snprintf(u->zip_url, sizeof u->zip_url, "%s", zip);
    snprintf(u->sha_url, sizeof u->sha_url, "%s", sha);
    u->avail = 1;
  }
  return 0;
}

void upd_check_start(UpdState *u) {
  static int started;
  if (started) return;
  started = 1;
  g_u = u;
  const char *feed = getenv("SIMGOLF_UPDATE_FEED");
  if (!mods_get_int("updates.check", 1)) {
    fprintf(stderr, "update: Check for updates is off, no network is used\n");
    return;
  }
  if (!strcmp(SIMGOLF_VERSION, "dev") && !(feed && *feed)) {
    fprintf(stderr, "update: development build, no check\n");
    return;
  }
  HANDLE h = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
  if (h) CloseHandle(h);
}

// ------------------------------------------------------------------ Update now

static void status(UpdState *u, int busy, const char *text) {
  snprintf(u->status_text, sizeof u->status_text, "%s", text);
  u->busy = busy;
  fprintf(stderr, "update: %s\n", text);
}

// Prepare owns the extraction timeout and reaps tar before it exits. An outer
// timeout would abandon prepare (and its tar child), allowing a retry to reuse
// the same stage while extraction is still writing there.
static int run_wait(char *cmd) {
  STARTUPINFOA si = {sizeof si};
  PROCESS_INFORMATION pi;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
  while (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) Sleep(100);
  DWORD code = (DWORD)-1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)code;
}

static DWORD WINAPI install_thread(LPVOID arg) {
  UpdState *u = (UpdState *)arg;
  char app[MAX_PATH], dir[MAX_PATH + 16], zipf[MAX_PATH + 64], shaf[MAX_PATH + 64], stage[MAX_PATH + 64], cmd[3 * MAX_PATH];
  app_dir(app, sizeof app);
  snprintf(dir, sizeof dir, "%s\\updates", app);
  CreateDirectoryA(dir, NULL);
  snprintf(zipf, sizeof zipf, "%s\\update-%s.zip", dir, u->latest);
  snprintf(shaf, sizeof shaf, "%s\\update-%s.zip.sha256", dir, u->latest);
  snprintf(stage, sizeof stage, "%s\\stage-%s", dir, u->latest);
  status(u, 1, "Downloading the update");
  if (!fetch(u->sha_url, NULL, NULL, 0, shaf, DOWNLOAD_TIMEOUT_MS) || !fetch_progress(u->zip_url, NULL, NULL, 0, zipf, DOWNLOAD_TIMEOUT_MS,u)) {
    remove(shaf);
    remove(zipf);
    status(u, 2, "The download did not work. Nothing was changed.");
    return 0;
  }
  InterlockedExchange((LONG *)&u->download_per_mille,1000);
  status(u, 1, "Checking checksum and unpacking files");
  snprintf(cmd, sizeof cmd, "\"%s\\sgupdate.exe\" --prepare --zip \"%s\" --sha \"%s\" --stage \"%s\"", app, zipf, shaf, stage);
  int rc = run_wait(cmd);
  if (rc != 0) {
    status(u, 2, rc == 2 ? "The download does not match its checksum, so it was not used. Nothing was changed."
                         : "The download could not be unpacked. Nothing was changed.");
    remove(zipf);
    remove(shaf);
    return 0;
  }
  const char *noapply = getenv("SIMGOLF_UPDATE_NO_APPLY");
  if (noapply && *noapply) {
    status(u, 3, "Downloaded and checked. Ready to install.");
    return 0;
  }
  // the updater runs from a copy: it replaces sgupdate.exe itself
  char run[MAX_PATH + 64], src[MAX_PATH + 16];
  snprintf(src, sizeof src, "%s\\sgupdate.exe", app);
  snprintf(run, sizeof run, "%s\\run-sgupdate.exe", dir);
  if (!CopyFileA(src, run, FALSE)) {
    status(u, 2, "The updater could not start. Nothing was changed.");
    return 0;
  }
  snprintf(cmd, sizeof cmd, "\"%s\" --apply --app \"%s\" --stage \"%s\\app\" --wait-pid %lu --restart", run, app, stage,
           (unsigned long)GetCurrentProcessId());
  STARTUPINFOA si = {sizeof si};
  PROCESS_INFORMATION pi;
  // The native address-reservation launcher owns the game in a kill-on-close
  // job. This updater must outlive the game process so it can wait for the EXE
  // lock to clear, apply the release and relaunch the player program.
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB, NULL, app, &si, &pi)) {
    status(u, 2, "The updater could not start. Nothing was changed.");
    return 0;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  status(u, 1, "Installing: the game restarts in a moment");
  SDL_Event q = {0};
  q.type = SDL_QUIT;
  SDL_PushEvent(&q);
  return 0;
}

void upd_install_start(UpdState *u) {
  if (u->busy == 1 || !u->avail) return;
  u->download_per_mille=-1;u->download_kb=u->download_total_kb=0;
  u->busy = 1;
  HANDLE h = CreateThread(NULL, 0, install_thread, u, 0, NULL);
  if (h) CloseHandle(h);
  else status(u, 2, "The update could not start.");
}
