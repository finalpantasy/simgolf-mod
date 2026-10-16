#ifndef SIMGOLF_RELEASE_DATA_H
#define SIMGOLF_RELEASE_DATA_H
#include <windows.h>
#include <stdio.h>
// The 0.1.16 updater copies data/tables recursively. Its fixed whitelist
// predates the new optional data files at data/. Only a missing primary
// uses the compatibility copy; existing custom or inaccessible files stay put.
static inline void release_data_path(char *out,size_t size,const char *data_dir,const char *name) {
  snprintf(out,size,"%s/%s",data_dir,name);
  if(GetFileAttributesA(out)!=INVALID_FILE_ATTRIBUTES)return;
  DWORD error=GetLastError();
  if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)
    snprintf(out,size,"%s/tables/_release/%s",data_dir,name);
}
#endif
