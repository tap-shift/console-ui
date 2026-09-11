# Console UI (appliance-mode dashboard)

![C99](https://img.shields.io/badge/C-99-blue.svg)
![Raylib 6.0](https://img.shields.io/badge/Raylib-6.0-red.svg)
![Gamescope](https://img.shields.io/badge/Gamescope-supported-brightgreen.svg)
![Arch Linux](https://img.shields.io/badge/Arch_Linux-compatible-blue.svg)

A minimal, production-ready full-screen appliance console UI designed for an Intel x86_64 Arch Linux box.

## Features

- **Performance**: Targets 1080p resolution at a locked 60FPS.
- **Navigation**: Full support for both gamepad/controller and keyboard navigation.
- **Design**: Modern dark theme with a clean, modular horizontal card layout.
- **Deployment**: Designed for minimal DRM/KMS appliance deployment inside Gamescope.

## Build Instructions

### Prerequisites
Ensure you have the following installed:
- `gcc` (with C99 support)
- `cmake`
- `raylib` (development headers and libraries)

### Building
To build the project using CMake, run the following commands:

```bash
cmake -B build
cmake --build build
```

Alternatively, a fallback Makefile is provided. You can simply run:

```bash
make
```

## Running Instructions

To launch the compiled application directly with Gamescope, use:

```bash
gamescope -W 1920 -H 1080 -f -- ./build/console_ui
```
*(Adjust the path to `./console_ui` if you built using the fallback Makefile)*

## License

Copyright (c) 2026 tap-shift. All rights reserved.

This software is proprietary. Copying, modification, redistribution, sublicensing, reverse engineering, or commercial/non-commercial use is strictly prohibited. See the [LICENSE](LICENSE) file for more details.
