#!/bin/bash

abort() {
  echo "ERROR: $1!"
  exit 1
}

buildutil() {
  UTILS=(
    "AppleEfiSignTool"
    "ACPIe"
    "EfiResTool"
    "LogoutHook"
    "acdtinfo"
    "disklabel"
    "icnspack"
    "macserial"
    "ocpasswordgen"
    "ocvalidate"
    "TestBmf"
    "TestCpuFrequency"
    "TestDiskImage"
    "TestHelloWorld"
    "TestImg4"
    "TestKextInject"
    "TestMacho"
    "TestMp3"
    "TestExt4Dxe"
    "TestNtfsDxe"
    "TestPeCoff"
    "TestProcessKernel"
    "TestRsaPreprocess"
    "TestSmbios"
  )

  if [ "$HAS_OPENSSL_BUILD" = "1" ]; then
    UTILS+=("RsaTool")
  fi

  local cores
  cores=$(getconf _NPROCESSORS_ONLN)

  pushd "${selfdir}/Utilities" || exit 1
  for util in "${UTILS[@]}"; do
    cd "$util" || exit 1
    echo "构建 ${util}..."
    make clean || exit 1
    make -j "$cores" &>/dev/null || exit 1
    #
    # FIXME: Do not build RsaTool for Win32 without OpenSSL.
    #
    if [ "$util" = "RsaTool" ] && [ "$HAS_OPENSSL_W32BUILD" != "1" ]; then
      continue
    fi

    if [ "$(which i686-w64-mingw32-gcc)" != "" ]; then
      echo "为windows构建 ${util}..."
      UDK_ARCH=Ia32 CC=i686-w64-mingw32-gcc STRIP=i686-w64-mingw32-strip DIST=Windows make clean || exit 1
      UDK_ARCH=Ia32 CC=i686-w64-mingw32-gcc STRIP=i686-w64-mingw32-strip DIST=Windows make -j "$cores" || exit 1
    fi
    if [ "$(which x86_64-linux-musl-gcc)" != "" ]; then
      echo "Building ${util} for Linux..."
      STATIC=1 SUFFIX=.linux UDK_ARCH=X64 CC=x86_64-linux-musl-gcc STRIP=x86_64-linux-musl-strip DIST=Linux make clean || exit 1
      STATIC=1 SUFFIX=.linux UDK_ARCH=X64 CC=x86_64-linux-musl-gcc STRIP=x86_64-linux-musl-strip DIST=Linux make -j "$cores" || exit 1
    fi
    cd - || exit 1
  done
  popd || exit
}

package() {
  if [ ! -d "$1" ]; then
    echo "丢失包目录$1"
    exit 1
  fi

  local ver
  ver=$(grep OPEN_CORE_VERSION ./Include/Acidanthera/Library/OcMainLib.h | sed 's/.*"\(.*\)".*/\1/' | grep -E '^[0-9.]+$')
  if [ "$ver" = "" ]; then
    echo "无效版本 $ver"
    ver="UNKNOWN"
  fi

  selfdir=$(pwd)
  pushd "$1" || exit 1
  rm -rf tmp || exit 1

  dirs=(
    "tmp/Docs/AcpiSamples"
    "tmp/Utilities"
    )
  for dir in "${dirs[@]}"; do
    mkdir -p "${dir}" || exit 1
  done

  efidirs=(
    "EFI/BOOT"
    "EFI/OC/ACPI"
    "EFI/OC/Drivers"
    "EFI/OC/Kexts"
    "EFI/OC/Tools"
    "EFI/OC/Resources/Audio"
    "EFI/OC/Resources/Font"
    "EFI/OC/Resources/Image"
    "EFI/OC/Resources/Label"
    )

  # Switch to parent architecture directory (i.e. Build/X64 -> Build).
  local dstdir
  dstdir="$(pwd)/tmp"
  pushd .. || exit 1

  for arch in "${ARCHS[@]}"; do
    for dir in "${efidirs[@]}"; do
      mkdir -p "${dstdir}/${arch}/${dir}" || exit 1
    done

    # copy OpenCore main program.
    cp "${arch}/OpenCore.efi" "${dstdir}/${arch}/EFI/OC" || exit 1
    printf "%s" "OpenCore" > "${dstdir}/${arch}/EFI/OC/.contentFlavour" || exit 1
    printf "%s" "Disabled" > "${dstdir}/${arch}/EFI/OC/.contentVisibility" || exit 1

    local suffix="${arch}"
    if [ "${suffix}" = "X64" ]; then
      suffix="x64"
    fi
    cp "${arch}/Bootstrap.efi" "${dstdir}/${arch}/EFI/BOOT/BOOT${suffix}.efi" || exit 1
    printf "%s" "OpenCore" > "${dstdir}/${arch}/EFI/BOOT/.contentFlavour" || exit 1
    printf "%s" "Disabled" > "${dstdir}/${arch}/EFI/BOOT/.contentVisibility" || exit 1

    efiTools=(
      "BootKicker.efi"
      "ChipTune.efi"
      "CleanNvram.efi"
      "CsrUtil.efi"
      "GopStop.efi"
      "KeyTester.efi"
      "MmapDump.efi"
      "ResetSystem.efi"
      "RtcRw.efi"
      "TpmInfo.efi"
      "OpenControl.efi"
      "ControlMsrE2.efi"
      )
    for efiTool in "${efiTools[@]}"; do
      cp "${arch}/${efiTool}" "${dstdir}/${arch}/EFI/OC/Tools"/ || exit 1
    done

    # Special case: OpenShell.efi
    cp "${arch}/Shell.efi" "${dstdir}/${arch}/EFI/OC/Tools/OpenShell.efi" || exit 1
    cp -r "${selfdir}/Resources/" "${dstdir}/${arch}/EFI/OC/Resources"/ || exit 1

    efiDrivers=(
      "ArpDxe.efi"
      "AudioDxe.efi"
      "BiosVideo.efi"
      "CrScreenshotDxe.efi"
      "Dhcp4Dxe.efi"
      "DnsDxe.efi"
      "DpcDxe.efi"
      "Ext4Dxe.efi"
      "HiiDatabase.efi"
      "HttpBootDxe.efi"
      "HttpDxe.efi"
      "HttpUtilitiesDxe.efi"
      "Ip4Dxe.efi"
      "MnpDxe.efi"
      "NvmExpressDxe.efi"
      "OpenCanopy.efi"
      "OpenHfsPlus.efi"
      "OpenLinuxBoot.efi"
      "OpenNtfsDxe.efi"
      "OpenPartitionDxe.efi"
      "OpenRuntime.efi"
      "OpenUsbKbDxe.efi"
      "OpenVariableRuntimeDxe.efi"
      "Ps2KeyboardDxe.efi"
      "Ps2MouseDxe.efi"
      "ResetNvramEntry.efi"
      "SnpDxe.efi"
      "TcpDxe.efi"
      "ToggleSipEntry.efi"
      "Udp4Dxe.efi"
      "UsbMouseDxe.efi"
      "XhciDxe.efi"
      )
    for efiDriver in "${efiDrivers[@]}"; do
      cp "${arch}/${efiDriver}" "${dstdir}/${arch}/EFI/OC/Drivers"/ || exit 1
    done
  done

  docs=(
    "Configuration.pdf"
    "Differences/Differences.pdf"
    "Sample.plist"
    "SampleCustom.plist"
    )
  for doc in "${docs[@]}"; do
    cp "${selfdir}/Docs/${doc}" "${dstdir}/Docs"/ || exit 1
  done
  cp "${selfdir}/Changelog.md" "${dstdir}/Docs"/ || exit 1
  cp -r "${selfdir}/Docs/AcpiSamples/"* "${dstdir}/Docs/AcpiSamples"/ || exit 1

  mkdir -p "${dstdir}/Docs/AcpiSamples/Binaries" || exit 1
  cd "${dstdir}/Docs/AcpiSamples/Source" || exit 1
  for i in *.dsl ; do
    iasl "$i" || exit 1
  done
  mv ./*.aml "${dstdir}/Docs/AcpiSamples/Binaries" || exit 1
  cd - || exit 1

  utilScpts=(
    "LegacyBoot"
    "CreateVault"
    "FindSerialPort"
    "macrecovery"
    "kpdescribe"
    "ShimToCert"
    )
  for utilScpt in "${utilScpts[@]}"; do
    cp -r "${selfdir}/Utilities/${utilScpt}" "${dstdir}/Utilities"/ || exit 1
  done

  buildutil || exit 1

  # Copy LogoutHook.
  mkdir -p "${dstdir}/Utilities/LogoutHook" || exit 1
  logoutFiles=(
    "Launchd.command"
    "Launchd.command.plist"
    "README.md"
    "nvramdump"
    )
  for file in "${logoutFiles[@]}"; do
    cp "${selfdir}/Utilities/LogoutHook/${file}" "${dstdir}/Utilities/LogoutHook"/ || exit 1
  done

  # Copy OpenDuetPkg booter.
  for arch in "${ARCHS[@]}"; do
    local tgt
    local booter
    tgt="$(basename "$(pwd)")"
    booter="$(pwd)/../../OpenDuetPkg/${tgt}/${arch}/boot"

    if [ -f "${booter}" ]; then
      echo "从 ${booter} 拷贝OpenDuetPkg启动文件..."
      cp "${booter}" "${dstdir}/Utilities/LegacyBoot/boot${arch}" || exit 1
    else
      echo "在${booter}找不到OpenDuetPkg!"
    fi
  done

  utils=(
    "ACPIe"
    "acdtinfo"
    "macserial"
    "ocpasswordgen"
    "ocvalidate"
    "disklabel"
    "icnspack"
    )
  for util in "${utils[@]}"; do
    dest="${dstdir}/Utilities/${util}"
    mkdir -p "${dest}" || exit 1
    bin="${selfdir}/Utilities/${util}/${util}"
    cp "${bin}" "${dest}" || exit 1
    if [ -f "${bin}.exe" ]; then
      cp "${bin}.exe" "${dest}" || exit 1
    fi
    if [ -f "${bin}.linux" ]; then
      cp "${bin}.linux" "${dest}" || exit 1
    fi
  done
  # additional docs for macserial.
  cp "${selfdir}/Utilities/macserial/FORMAT.md" "${dstdir}/Utilities/macserial"/ || exit 1
  cp "${selfdir}/Utilities/macserial/README.md" "${dstdir}/Utilities/macserial"/ || exit 1
  # additional docs for ocvalidate.
  cp "${selfdir}/Utilities/ocvalidate/README.md" "${dstdir}/Utilities/ocvalidate"/ || exit 1

  pushd "${dstdir}" || exit 1
  zip -qr -FS ../"OpenCore-Mod-${ver}-${2}.zip" ./* || exit 1
  popd || exit 1
  rm -rf "${dstdir}" || exit 1

  popd || exit 1
  popd || exit 1
}

cd "$(dirname "$0")" || exit 1
if [ "$ARCHS" = "" ]; then
  ARCHS=(X64 IA32)
  export ARCHS
fi
SELFPKG=OpenCorePkg
NO_ARCHIVES=0
DISCARD_PACKAGES=OpenCorePkg

export SELFPKG
export NO_ARCHIVES
export DISCARD_PACKAGES

src=$(curl -Lfs https://gitee.com/btwise/ocbuild/raw/master/efibuild.sh) && eval "$src" || exit 1

cd Utilities/ocvalidate || exit 1
ocv_tool=""
if [ -x ./ocvalidate ]; then
  ocv_tool=./ocvalidate
elif [ -x ./ocvalidate.exe ]; then
  ocv_tool=./ocvalidate.exe
fi

if [ -x "$ocv_tool" ]; then
  "$ocv_tool" ../../Docs/Sample.plist || abort "Wrong Sample.plist"
  "$ocv_tool" ../../Docs/SampleCustom.plist || abort "Wrong SampleCustom.plist"
fi
cd ../..

cd Library/OcConfigurationLib || exit 1
echo "编译成功!"
echo "----------------------------------------------------------------"
echo "运行检查架构脚本......"
./CheckSchema.py OcConfigurationLib.c >/dev/null || abort "OccConfigurationLib.c错误"
echo "架构检查完成！"
echo "编译成功!" && open $BUILDDIR/Binaries
