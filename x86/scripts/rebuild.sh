#!/bin/bash
cd /tmp/kresten/x86
make plugins || exit 1
echo BUILD_DONE=0
make || exit 1
echo BUILD_DONE=0
make install-iso || exit 1
echo BUILD_DONE=0
bash scripts/make_iso_x86.sh
