// Bounded CPU jobs. The caller owns all inputs until every worker has joined.
#pragma once
#include <windows.h>
#ifndef SG_CREATE_THREAD
#define SG_CREATE_THREAD CreateThread
#endif
typedef void (*SgJobFn)(void *, int);
typedef void (*SgJobFinish)(void);
typedef struct SgJobs {
  volatile LONG next;
  int count;
  SgJobFn run;
  SgJobFinish finish;
  void *user;
} SgJobs;
static void sg_jobs_work(SgJobs *s) {
  for (;;) {
    LONG i = InterlockedIncrement(&s->next) - 1;
    if (i >= s->count) return;
    s->run(s->user, (int)i);
  }
}
static DWORD WINAPI sg_jobs_thread(void *arg) {
  SgJobs *s = arg;
  sg_jobs_work(s);
  if (s->finish) s->finish();
  return 0;
}
static int sg_run_jobs(int count, SgJobFn run, SgJobFinish finish, void *user) {
  SgJobs s = {0, count, run, finish, user};
  HANDLE workers[3];
  int n = 0;
  if (count >= 8) for (int i = 0; i < 3; i++) {
    HANDLE t = SG_CREATE_THREAD(NULL, 0, sg_jobs_thread, &s, 0, NULL);
    if (t) workers[n++] = t;
  }
  sg_jobs_work(&s);
  if (n) WaitForMultipleObjects((DWORD)n, workers, TRUE, INFINITE);
  for (int i = 0; i < n; i++) CloseHandle(workers[i]);
  return n;
}
