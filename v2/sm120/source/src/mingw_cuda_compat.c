// SPDX-License-Identifier: GPL-3.0-or-later
// CUDA's MSVC-built loader references the CRT floating-point marker.
// MinGW initializes its own CRT and does not provide this linker marker.
int _fltused = 0x9875;
