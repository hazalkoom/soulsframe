======================================================================
SoulsFrame v1.0.0 -- Dark Souls III Performance Mod (Quick Setup)
======================================================================

IMPORTANT -- OFFLINE ONLY:
SoulsFrame injects dinput8.dll and hooks DirectX 11 at runtime.
NEVER launch Dark Souls III online with these files installed.
Always switch Steam / Dark Souls III to Offline Mode first.


----------------------------------------------------------------------
STEP-BY-STEP INSTALLATION
----------------------------------------------------------------------

1. Open your Dark Souls III "Game" folder (the folder that contains
   DarkSoulsIII.exe).
   Example:
   C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS III\Game

2. Copy ALL files and folders from this zip directly next to
   DarkSoulsIII.exe:
     - dinput8.dll
     - soulsframe.toml
     - presets\  (optional ready-made configs)

3. (Optional) Pick a ready-made config preset for your PC:
   Copy one file from the "presets" folder next to DarkSoulsIII.exe
   and rename it to soulsframe.toml (replacing the default file):
     - presets\safe-frame-pacing.toml
         Smoother 60 FPS frame pacing + FPS HUD (zero visual changes).
     - presets\low-end-gpu.toml
         60 FPS pacing + lower particle lag + FSR 1 Quality upscaling
         + FPS HUD. Best choice for weak or integrated GPUs.

4. Open soulsframe.toml in Notepad, find this line near the top:
     offline_acknowledged = false
   Change it to:
     offline_acknowledged = true
   Save and close the file.

   (Without this safety opt-in, SoulsFrame blocks all hooks.)

5. Switch Steam / Dark Souls III to Offline Mode, then start the game.
   * Linux / Steam Deck (Proton) only: set DS3 Steam Launch Options to:
     WINEDLLOVERRIDES="dinput8=n,b" %command%

6. In the game, check the top-left corner for the FPS HUD ("SF v1.0.0").
   Runtime logs are saved in soulsframe.log in the same folder.

   To UNINSTALL anytime: delete dinput8.dll (and soulsframe.toml /
   soulsframe.log) from the Game folder.


----------------------------------------------------------------------
IN-GAME HOTKEYS (NO GAME RESTART NEEDED)
----------------------------------------------------------------------

Default hotkeys work while playing (when modules.overlay = true and
[hotkeys] enabled = true in soulsframe.toml):

  F9   -- Show or hide the FPS counter and hotkey status box.

  F10  -- Master ON/OFF switch (pause or resume ALL mod optimizations
          live so you can compare before vs after FPS instantly;
          HUD displays PAUSED).

  F11  -- Toggle FSR 1 upscaling & sharpening ON/OFF live
          (works when modules.upscaler = true, e.g. low-end-gpu.toml).

  F8   -- Toggle particle lag reduction ON/OFF live
          (works when modules.particles = true, e.g. low-end-gpu.toml).

  F12  -- Toggle the FPS cap (present.max_fps) ON/OFF live
          (works when modules.presentation = true and max_fps > 0).


----------------------------------------------------------------------
CUSTOMIZING KEYS & HUD
----------------------------------------------------------------------

Open soulsframe.toml in Notepad to change:
  - [overlay] position = "top_left" | "top_right" | "bottom_left" | "bottom_right"
  - [overlay] scale = 2 (1 to 4), opacity_percent = 75
  - [hotkeys] change any key to F1-F12, Insert, Delete, Home, End,
    PageUp, PageDown, Pause, ScrollLock, Numpad0-9, or "none"
  - Disable Steam Overlay / RTSS / ReShade if you notice hook conflicts.
