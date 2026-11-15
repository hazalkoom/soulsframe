#include <windows.h>

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, LPVOID*, LPUNKNOWN);

/// Resolves `DirectInput8Create` from the real system `dinput8.dll` in System32
/// so game controller/keyboard input keeps working transparently.
DirectInput8CreateFn resolve_real_dinput8_create() {
  static DirectInput8CreateFn fn = []() -> DirectInput8CreateFn {
    char dir[MAX_PATH]{};
    const UINT n = GetSystemDirectoryA(dir, MAX_PATH);
    if (n == 0 || n + 13 >= MAX_PATH) {
      return nullptr;
    }
    lstrcatA(dir, "\\dinput8.dll");
    HMODULE mod = LoadLibraryA(dir);
    if (!mod) {
      return nullptr;
    }
    // Route through void* to satisfy -Wcast-function-type on MinGW GCC.
    void* sym = reinterpret_cast<void*>(GetProcAddress(mod, "DirectInput8Create"));
    return reinterpret_cast<DirectInput8CreateFn>(sym);
  }();
  return fn;
}

} // namespace

/// Exported `dinput8.dll` proxy entry point expected by DarkSoulsIII.exe.
extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion,
                                                                   const GUID& riidltf,
                                                                   LPVOID* ppvOut,
                                                                   LPUNKNOWN punkOuter) {
  auto fn = resolve_real_dinput8_create();
  return fn ? fn(hinst, dwVersion, riidltf, ppvOut, punkOuter) : E_FAIL;
}
