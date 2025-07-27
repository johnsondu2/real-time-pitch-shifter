# Real-Time Pitch Shifter

A real-time audio pitch-shifting tool built in C++ using PortAudio. It reads from your microphone input, applies a pitch shift, and plays the modified audio in real time.

## Prerequisites

- C++ compiler (`g++`)
- [PortAudio](http://www.portaudio.com/) library
- (Windows only) MSYS2 or vcpkg for easier dependency management
- (macOS only) Homebrew package manager

## Compilation and Execution Instructions

### 1. Install PortAudio

#### Windows

Use MSYS2 or vcpkg to install PortAudio, and compile using MinGW: pacman -S mingw-w64-x86_64-portaudio

#### Mac

Install PortAudio using Homebrew: brew install portaudio

---

### 2: Compile the Program

Navigate to the folder containing the source file.

#### Windows (with MSYS2 and PortAudio installed):

g++ task2.3.cpp smbPitchShift.cpp -lportaudio -o task2.3.exe

#### macOS with Homebrew:

g++ task2.3.cpp smbPitchShift.cpp \
 -I/opt/homebrew/opt/portaudio/include \
 -L/opt/homebrew/opt/portaudio/lib \
 -lportaudio -o task2.3

### 3: Run the Program

#### Windows:

task2.3

#### Mac:

./task2.3
