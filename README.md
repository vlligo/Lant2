# Langton's Ant Simulator

A Qt-based implementation of Langton's Ant with advanced visualization, statistics tracking, and interactive controls.

## Features

- **Full Langton's Ant Simulation**: Classic cellular automaton with customizable rules
- **Interactive Visualization**:
  - Zoom and pan functionality
  - Adjustable cell size
  - Real-time ant tracking
  - Grid display with coordinate system
- **Comprehensive Statistics**:
  - Visit counts per cell
  - Corner traversal statistics
  - Most visited cells tracking
  - Unique cells visited
  - Average visits calculation
- **Performance Optimizations**:
  - Efficient cell state storage using hash tables
  - Batched statistics updates
  - Smart rendering with buffering
  - Dynamic grid bounds expansion
- **User Interface**:
  - Multiple rule presets
  - Custom rule input with compression (e.g., "L3R2")
  - Step-by-step or batch simulation
  - Statistics panel with top visited cells
  - Export to CSV functionality

## Requirements

- **Qt 6.10.1** (compatible with Qt 5+)
- **CMake 3.16** or higher
- **C++17** compatible compiler
- **Ninja** build system (recommended)

## Building

### Linux
```bash
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/qt -DCMAKE_GENERATOR:STRING=Ninja ..
cmake --build .
```

### Windows
```bash
mkdir build && cd build
cmake -G "Ninja" -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\msvc2019_64" ..
cmake --build .
```

### macOS
```bash
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH="/Users/username/Qt/6.10.1/clang_64" ..
cmake --build .
```

## Usage

### Basic Controls
1. **Rules**: Enter Langton's Ant rules using 'L' (left turn) and 'R' (right turn) characters
   - Supports compression: "L3R2" = "LLLRR"
   - Presets available: Classic LR, Symmetric LLRR, Highway, Complex

2. **Simulation**:
   - **Step**: Take one simulation step
   - **Run**: Execute multiple steps at once
   - **Reset**: Clear simulation and statistics

3. **View Controls**:
   - **Zoom**: Mouse wheel or buttons
   - **Pan**: Click and drag
   - **Center**: Center on ant, most visited cell, or coordinates
   - **Navigation**: Arrow buttons for precise view movement

4. **Statistics**:
   - Toggle statistics tracking
   - View top visited cells in table
   - Export data to CSV
   - Show detailed statistics dialog

### Mouse Interaction
- **Hover**: Shows cell coordinates in status bar
- **Click+Drag**: Pan the view
- **Wheel**: Zoom in/out centered on cursor

### Rule Examples
- `LR`: Classic Langton's Ant
- `LLRR`: Symmetrical pattern
- `LLRRRLRLRLLR`: Complex pattern
- `L1R1`: Equivalent to `LR`

## Development Notes

### Code Style
- Uses Qt coding conventions
- C++17 features enabled
- CMake with Qt integration
- Manual finalization for Qt 6 compatibility

### Dependencies
- Qt Core, Gui, and Widgets modules
- Standard C++ library
- System-specific: X11 on Linux, Cocoa on macOS, Win32 on Windows

### Platform Support
- **Linux**: Tested with GCC
- **Windows**: Requires MSVC or MinGW
- **macOS**: Requires Xcode toolchain

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## Acknowledgments

- Based on Langton's Ant, invented by Chris Langton (1986)
- Built with Qt framework
- CMake build system integration
