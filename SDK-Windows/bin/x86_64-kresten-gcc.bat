@echo off
set KRESTEN_SDK_ROOT=%~dp0..
set KRESTEN_SDK_ROOT=%KRESTEN_SDK_ROOT:\=/
x86_64-w64-mingw32-gcc -m64 -nostdlib -ffreestanding -fno-pie -mno-red-zone -Wall -Werror -DX86_ARCH -I"%KRESTEN_SDK_ROOT%sysroot/include" -L"%KRESTEN_SDK_ROOT%sysroot/lib" %*