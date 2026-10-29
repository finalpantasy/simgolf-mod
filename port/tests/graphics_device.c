#define SDL_MAIN_HANDLED
#define COBJMACROS
#include <SDL.h>
#include <SDL_system.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>
static ID3D11Device device;
static int refs=1,missing,stored,checks,failures;
static HRESULT read_result=S_OK,write_result=S_OK,health=S_OK;
static UINT bytes=sizeof(UINT_PTR);
static UINT_PTR marker;
static ULONG STDMETHODCALLTYPE release(ID3D11Device *d){(void)d;return --refs;}
static HRESULT STDMETHODCALLTYPE get_data(ID3D11Device *d,REFGUID key,UINT *size,void *value){
  (void)d;(void)key;if(FAILED(read_result))return read_result;
  if(!stored)return E_FAIL;*size=bytes;memcpy(value,&marker,sizeof marker);return S_OK;
}
static HRESULT STDMETHODCALLTYPE set_data(ID3D11Device *d,REFGUID key,UINT size,const void *value){
  (void)d;(void)key;if(FAILED(write_result))return write_result;
  memcpy(&marker,value,sizeof marker);bytes=size;stored=1;return S_OK;
}
static HRESULT STDMETHODCALLTYPE removed(ID3D11Device *d){(void)d;return health;}
static ID3D11Device *test_device(SDL_Renderer *r){(void)r;if(missing)return NULL;++refs;return &device;}
#define SDL_RenderGetD3D11Device test_device
#include "port/graphics_device.h"
static void check(int ok,const char *what){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",what);}}
int main(void){
  ID3D11DeviceVtbl v={0};v.Release=release;v.GetPrivateData=get_data;v.SetPrivateData=set_data;v.GetDeviceRemovedReason=removed;device.lpVtbl=&v;
  SDL_Renderer *r=(SDL_Renderer *)1;
  check(!hd_device_matches(r,42),"unmarked replacement detected without event");
  check(hd_device_mark(r,42),"mark first device");
  check(hd_device_matches(r,42),"same healthy device");
  check(!hd_device_matches(r,43),"different owner rejected");
  stored=0;check(!hd_device_matches(r,42),"replacement at reused address detected");
  check(hd_device_mark(r,42),"mark recovered device");
  health=E_FAIL;check(!hd_device_matches(r,42),"removed device detected");health=S_OK;
  bytes=1;check(!hd_device_matches(r,42),"wrong marker size rejected");bytes=sizeof marker;
  read_result=E_FAIL;check(!hd_device_matches(r,42),"private-data read failure");read_result=S_OK;
  write_result=E_FAIL;check(!hd_device_mark(r,42),"private-data write failure");write_result=S_OK;
  missing=1;check(!hd_device_matches(r,42),"missing device detected");check(!hd_device_mark(r,42),"missing device not marked");missing=0;
  for(int i=0;i<10000;i++)if(!hd_device_matches(r,42))failures++;
  check(refs==1,"every borrowed reference released, including failures");
  printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
