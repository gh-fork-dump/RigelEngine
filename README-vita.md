Very janky port of the RigelEngine to the PlayStation Vita. The gameplay is pretty much flawless,
except for one big issue: the game crashes on the intro movie, or when opening the "Instructions &
Story" section in the menu. The game plays something like 0.25s of music before hard crashing. I
can't figure out if it's a driver error or a code error, as the backtrace location when examining
the coredumps change on every build.

The steps to build are:

1. Clone [PVR_PSP2](https://github.com/GrapheneCt/PVR_PSP2),
   [SDL2](https://github.com/libsdl-org/SDL) and
   [RigelEngine](https://github.com/lethal-guitar/RigelEngine/) from github.
1. Copy all the headers in the `include` directory into
   `$VITASDK/arm-vita-eabi/include`, overwriting the exisiting ones used by vitaGL.
1. Download the latest `PSVita_Release.zip` and `vitasdk_stubs.zip` assets located
   [here](https://github.com/GrapheneCt/PVR_PSP2/releases/latest). Extract all the `.a` archives in
   `vitasdk_stubs.zip` into `$VITASDK/arm-vita-eabi/lib`.
1. Build and install SDL2 with `PVR_PSP2` support as shown:
   ```sh
   cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE=${VITASDK}/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   cmake --install build
   ```
1. In the RigelEngine root directory, create the directory `module` and extract the following
   `.suprx` files from `PSVita_Release.zip` into it:
   - libGLESv2.suprx
   - libIMGEGL.suprx
   - libgpu_es4_ext.suprx
   - libpvrPSP2_WSEGL.suprx
1. Build RigelEngine using the same commands for SDL used above. If successful,
   `build/src/RigelEngine.vpk` should be produced.

Further things to finish if the crash gets fixed:

- Add LiveArea assets.
- Disable the quit game button on the main menu.
- Custom `main()` function like emscripten?
- Keep changes in `src/frontend` as non-invasive as possible.
