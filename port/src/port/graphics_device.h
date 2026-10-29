// D3D11 can discard every texture and then fail before SDL emits its reset
// event. Mark the device itself, without retaining a COM reference: retaining
// the old device/context can prevent its replacement swap chain from opening.
#pragma once
#include <SDL.h>
#include <SDL_system.h>
#include <d3d11.h>

static const GUID hd_device_key = {0x663b19ce,0x0b21,0x45d8,{0xab,0xe3,0x5b,0x39,0x8e,0xf4,0x7c,0x12}};

static int hd_device_mark(SDL_Renderer *r, UINT_PTR owner) {
  ID3D11Device *d=SDL_RenderGetD3D11Device(r);
  if(!d)return 0;
  HRESULT hr=ID3D11Device_SetPrivateData(d,&hd_device_key,sizeof owner,&owner);
  ID3D11Device_Release(d);
  return SUCCEEDED(hr);
}

static int hd_device_matches(SDL_Renderer *r, UINT_PTR owner) {
  ID3D11Device *d=SDL_RenderGetD3D11Device(r);
  if(!d)return 0;
  UINT_PTR found=0;UINT size=sizeof found;
  HRESULT hr=ID3D11Device_GetPrivateData(d,&hd_device_key,&size,&found);
  HRESULT health=ID3D11Device_GetDeviceRemovedReason(d);
  ID3D11Device_Release(d);
  return SUCCEEDED(hr) && SUCCEEDED(health) && size==sizeof found && found==owner;
}
