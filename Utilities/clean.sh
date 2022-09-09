#!/bin/bash
UTILS=(
    "AppleEfiSignTool"
    "EfiResTool"
    "LogoutHook"
    "disklabel"
    "icnspack"
    "macserial"
    "ocvalidate"
    "TestBmf"
    "TestDiskImage"
    "TestHelloWorld"
    "TestImg4"
    "TestKextInject"
    "TestMacho"
    "TestMp3"
    "TestPeCoff"
    "TestRsaPreprocess"
    "TestSmbios"
  )
  for util in "${UTILS[@]}"; do
    cd "$util" || exit 1
    echo "清除编译文件 ${util}..."
    make clean || exit 1
    rm -rf Windows_Ia32
    cd ..
  done
