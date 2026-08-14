#!/bin/bash
cd /tmp/kresten/x86
make || exit 1
echo BUILD_DONE=0
bash scripts/make_iso_x86.sh
