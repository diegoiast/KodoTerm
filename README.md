# CodoTerm

A modern C++ Qt6 widget that embeds a terminal emulator using `libvterm`.

## Features

- **Qt6 Integration**: Seamlessly integrates as a `QWidget`.
- **LibVTerm**: Uses the robust `libvterm` for VT100/xterm emulation.
- **PTY Support**: Handles pseudo-terminal interactions via `forkpty`.
- **Customizable**: Supports custom fonts and colors (basic mapping).

## Prerequisites

- C++17 compiler
- CMake 3.16+
- Qt6 (Core, Gui, Widgets)
- Linux (for `forkpty` support)

## Build

```bash
mkdir build
cd build
cmake ..
make
```

## Run Demo

```bash
./build/CodoTermDemo
```

## License

MIT

## Author

Diego Iastrubni <diegoiast@gmail.com>
