#pragma once

// Minimal Darwin-only compatibility shim for syntax-checking Linux HLSDK headers.
// Real Linux builds must use the platform <elf.h>; the Makefile only adds this
// include directory when `uname -s` is Darwin.
