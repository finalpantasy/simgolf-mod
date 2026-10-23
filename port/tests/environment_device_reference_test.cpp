// Ownership integration test with the deployed SDL2 and a hidden D3D11 renderer.
// Compile as a standalone TU: it includes the real initialization/release code.
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#define D3D11_NO_HELPERS
#define SDL_MAIN_HANDLED
#include <windows.h>
static bool fail_compiler;
static HMODULE WINAPI test_load_library(LPCSTR name) {
  return fail_compiler ? nullptr : LoadLibraryA(name);
}
#define LoadLibraryA test_load_library
#include "../src/port/environment_fx.cpp"
#undef LoadLibraryA

// Drawing is not exercised by this ownership test.
extern "C" WeatherSurfaceTuning weather_surface_defaults(void) { return {}; }

static ULONG references(ID3D11Device *device) {
  ID3D11Device_AddRef(device);
  return ID3D11Device_Release(device);
}

int main() {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO)) return 3;
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
  SDL_Window *window = SDL_CreateWindow("SimGolf ownership test", 0, 0, 32, 32, SDL_WINDOW_HIDDEN);
  SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
  ID3D11Device *device = renderer ? SDL_RenderGetD3D11Device(renderer) : nullptr;
  if (!device) { std::fprintf(stderr, "D3D11 unavailable: %s\n", SDL_GetError()); return 3; }
  ULONG baseline = references(device);
  int failures = 0;
  for (int fault = 0; fault < 2; ++fault) {
    fail_compiler = fault != 0;
    for (int cycle = 0; cycle < 12; ++cycle) {
      EnvironmentFxState state;
      bool ready = initialize_d3d(&state, renderer);
      if (ready == fail_compiler) {
        std::fprintf(stderr, "Unexpected init outcome: %s\n", state.detail);
        ++failures;
      }
      release_d3d(&state);
      release_d3d(&state); // teardown is idempotent
      ULONG after = references(device);
      if (after != baseline) {
        std::fprintf(stderr, "fault=%d cycle=%d device refs %lu, expected %lu\n", fault, cycle, after, baseline);
        ++failures;
      }
    }
  }
  ID3D11Device_Release(device);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  std::printf("D3D11 ownership: 24 init/cleanup cycles, %d failures\n", failures);
  return failures ? 1 : 0;
}
