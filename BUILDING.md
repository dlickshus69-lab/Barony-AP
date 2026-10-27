# Building Barony with Archipelago (Windows)

This builds your own `barony.exe` with the Archipelago code in it. You still play with the
art, music and maps from your Steam copy of Barony.

Barony's own Windows build instructions are thin, so expect this part to take some trying.
If a step fails, copy the error and we'll fix it together.

## 1. Install the tools

- **Git**: https://git-scm.com
- **Visual Studio 2022 Community** with the **Desktop development with C++** workload
  (it includes CMake)
- **vcpkg**, to get Barony's libraries:
  ```
  git clone https://github.com/microsoft/vcpkg C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
  C:\vcpkg\vcpkg install sdl2 sdl2-image sdl2-net sdl2-ttf physfs rapidjson libpng zlib dirent openal-soft libogg libvorbis --triplet x64-windows
  ```

## 2. Get Barony's code and add the Archipelago changes

```
cd C:\
git clone https://github.com/TurningWheel/Barony.git BaronyAP
cd BaronyAP
git checkout 962a5ce
git checkout -b archipelago
git am path\to\barony-archipelago.patch
git clone https://github.com/N00byKing/APCpp.git extern\APCpp
```

`git am` adds the four Archipelago files to `src\` and the hooks listed in `client\PATCHES.md`.

## 3. Build

```
cd C:\BaronyAP
mkdir build
cd build
cmake .. -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DOPENAL_ENABLED=ON
cmake --build . --config Release --target barony
```

- `-DOPENAL_ENABLED=ON` gives you sound through OpenAL. Barony's official sound library is
  FMOD, which needs a free FMOD account to download; without either one the game has no sound.
- If CMake can't find a library, Barony also reads a `BARONY_WIN32_LIBRARIES` environment
  variable pointing at a folder with `include\` and `lib\` inside. vcpkg's
  `C:\vcpkg\installed\x64-windows` has exactly that layout.

## 4. Put it together with your game files

Don't overwrite your Steam install. Make a separate copy:

1. Copy your whole Barony folder
   (`C:\Program Files (x86)\Steam\steamapps\common\Barony`) to `C:\Games\BaronyAP`.
2. From `build\Release`, copy `barony.exe`, `BaronyAPTracker.exe`, `APCpp.dll` and the
   SDL/libpng/zlib/physfs/openal DLLs into `C:\Games\BaronyAP`, replacing the old `barony.exe`.
3. Copy everything in `client\game-files` into `C:\Games\BaronyAP` (images, item icons, models).
4. Run `C:\Games\BaronyAP\barony.exe`. The Barony Archipelago window opens first: type the
   room's port (or paste the room link) and your name, then Connect & Play.

This build has no Steam features (achievements, Steam lobbies). Solo play works normally.

## 5. Test it

1. Install `barony.apworld` (double-click it, or put it in Archipelago's `custom_worlds`
   folder), generate a game with your YAML, and host it (archipelago.gg or locally).
2. Start the game and connect from the window. The message log should say **Archipelago: connected**.
3. Start a run and go down to floor 2: the server should show your floor check.
4. Type `/ap_trap minotaur` in chat to see the Minotaur warning start.
5. `/ap_status` shows your keys, lives and kills.

## Updating

When the APWorld's items or locations change, regenerate the ID header and rebuild:
```
python tools\gen_cpp_ids.py
copy client\src\archipelago_ids.hpp C:\BaronyAP\src\
```
