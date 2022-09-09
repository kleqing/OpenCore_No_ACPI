UEFI Debugging with GDB/LLDB
============================

These scripts provide support for easier UEFI code debugging on virtual machines like VMware Fusion
or QEMU. The code is based on [Andrei Warkentin](https://github.com/andreiw)'s
[DebugPkg](https://github.com/andreiw/andreiw-wip/tree/master/uefi/DebugPkg) with improvements
in macOS support, pretty printing, and bug fixing.

The general approach is as follows:

1. Build GdbSyms binary with EDK II type info in DWARF
1. Locate `EFI_SYSTEM_TABLE` in memory by its magic
1. Locate `EFI_DEBUG_IMAGE_INFO_TABLE` by its GUID
1. Map relocated images within GDB
1. Provide handy functions and pretty printers

#### Preparing Source Code

To get started, the compilation environment should be set up. Such commands can be found in `Docs/Configuration.pdf`, section `3.3 Contribution`.

By default EDK II optimises produced binaries, so to build a "real" debug binary one should target
`NOOPT`. Do be aware that it strongly affects resulting binary size:

```
build -a X64 -t XCODE5   -b NOOPT -p OpenCorePkg/OpenCorePkg.dsc # for macOS
build -a X64 -t CLANGPDB -b NOOPT -p OpenCorePkg/OpenCorePkg.dsc # for other systems
```

`GdbSyms.dll` is built as a part of OpenCorePkg, yet prebuilt binaries are also available:

- `GdbSyms/Bin/X64_XCODE5/GdbSyms.dll` is built with XCODE5

To wait for debugger connection on startup `WaitForKeyPress` functions from `OcMiscLib.h` can be
utilised. Do be aware that this function additionally calls `DebugBreak` function, which may
be broken at during GDB/LLDB init.

#### VMware Configuration

VMware Fusion contains a dedicated debugStub, which can be enabled by adding the following
lines to .vmx file. Afterwards vmware-vmx will listen on TCP ports 8832 and 8864 (on the host)
for 32-bit and 64-bit gdb connections respectively, similarly to QEMU:
```
debugStub.listen.guest32 = "TRUE"
debugStub.listen.guest64 = "TRUE"
```

In case the debugging session is remote the following lines should be appended:
```
debugStub.listen.guest32.remote = "TRUE"
debugStub.listen.guest64.remote = "TRUE"
```

To halt the virtual machine upon executing the first instruction the following line code be added.
Note, that it does not seem to work on VMware Fusion 11 and results in freezes:
```
monitor.debugOnStartGuest32 = "TRUE"
```

To force hardware breakpoints use the following line:
```
debugStub.hideBreakpoints = "TRUE"
```

Alternatively to force software INT 3 breakpoints use the following line:
```
debugStub.hideBreakpoints = "FALSE"
```

Depending on the version of VMware Fusion and the host macOS version, only one or other of the
above may work to successfully single step and hit breakpoints, so you may need to try both.

When setting up a virtual machine for debugging, it is useful to be able to enter UEFI firmware
settings easily. To stall during POST for 3 seconds add the following line. Pressing any key
during this pause will boot into firmware settings:
```
bios.bootDelay = "3000"
```

In order to test `AudioDxe.efi` in VMware Fusion, the `.vmx` file must contain the line:
```
sound.virtualDev = "hdaudio"
```

This is present by default in a VM set up as macOS guest, and may be added manually to a Linux guest.
Current `AudioDxe.efi` UEFI sound quality in VMware Fusion is not representative of the quality available on real hardware.

#### QEMU configuration

In addition to VMware it is also possible to use [QEMU](https://www.qemu.org). QEMU debugging
on macOS host is generally rather limited and slow, but it is enough for generic troubleshooting
when no macOS guest booting is required.

1. Build OVMF firmware in NOOPT mode to be able to debug it:

    ```
    git submodule init
    git submodule update     # to clone OpenSSL
    build -a X64 -t XCODE5   -b NOOPT -p OvmfPkg/OvmfPkgX64.dsc # for macOS
    build -a X64 -t CLANGPDB -b NOOPT -p OvmfPkg/OvmfPkgX64.dsc # for other systems
    ```

    To build OVMF with SMM support add `-D SMM_REQUIRE=1`. To build OVMF with serial debugging
    add `-D DEBUG_ON_SERIAL_PORT=1`.

    _Note_: Optionally, you may build OvmfPkg with its own build script, which is located within the OvmfPkg directory.
    Use e.g. `./build.sh -a X64` or  `./build.sh -a X64 -b NOOPT`; additional build arguments such as for serial debugging
    or SMM support may be appended to this.

2. Prepare launch directory with OpenCore as usual. For example, make a directory named
    `QemuRun` and `cd` to it. You should have a similar directory structure:

    ```
    .
    └── ESP
        └── EFI
            ├── BOOT
            │   └── BOOTx64.efi
            └── OC
                ├── OpenCore.efi
                └── config.plist
    ```

3. Run QEMU
    
    The OvmfPkg build script can also start the virtual machine which it has just built, e.g.
    `./build.sh -a X64 qemu -drive format=raw,file=fat:rw:ESP` should be sufficient to start OpenCore;
    all options after `qemu` are passed directly to QEMU. Starting QEMU this way uses file `OVMF.fd`
    from the OVMF build directory, which is `OVMF_CODE.fd` and `OVMF_VARS.fd` combined; you may prefer
    to follow the second example below which specifies these files separately.

    In the remaining examples `OVMF_BUILD` should point to the OVMF build directory, e.g.
    `$HOME/UefiWorkspace/Build/OvmfX64/NOOPT_XCODE5/FV`.

    To start QEMU directly, and with a more realistic machine architecture:

    ```
    qemu-system-x86_64 -L . -bios "$OVMF_BUILD/OVMF.fd" -drive format=raw,file=fat:rw:ESP \
      -machine q35 -m 2048 -cpu Penryn -smp 4,cores=2 -usb -device usb-mouse -gdb tcp::8864
    ```

    To run QEMU with SMM support; also specifying CODE and VARS separately; also specifying the port required to connect a debugger (next section):

    ```
    qemu-system-x86_64 -L . -global driver=cfi.pflash01,property=secure,value=on \
      -drive if=pflash,format=raw,unit=0,file="$OVMF_BUILD"/OVMF_CODE.fd,readonly=on \
      -drive if=pflash,format=raw,unit=1,file="$OVMF_BUILD"/OVMF_VARS.fd \
      -drive format=raw,file=fat:rw:ESP \
      -global ICH9-LPC.disable_s3=1 -machine q35,smm=on,accel=tcg -m 2048 -cpu Penryn \
      -smp 4,cores=2 -usb -device usb-mouse -gdb tcp::8864
    ```

    You may additionally pass `-S` flag to QEMU to stop at first instruction
    and wait for GDB connection. To use serial debugging add `-serial stdio`
    (this causes OpenCore console log output to go to the shell which started QEMU).

4. Key and mouse support when running OpenCore in QEMU:

    Enabling keyboard and mouse support is effectively the same as on a normal machine, however some typical options are:

    For keyboard support either, a) use `KeySupport=true`, or b) use `KeySupport=false`, in which case pass `-usb -device usb-kbd` to QEMU, and use `OpenUsbKbDxe.efi` driver.

    For mouse support either, a) use `Ps2MouseDxe.efi` driver, or b) pass `-usb -device usb-mouse` to QEMU and use `UsbMouseDxe.efi` driver.

5. Audio support in QEMU:

    QEMU flags to enable audio support in QEMU running on macOS host are `-audiodev coreaudio,id=audio0 -device ich9-intel-hda -device hda-output,audiodev=audio0`.
      - `coreaudio` is the macOS specific hardware audio driver
      - `intel-hda` may be used instead of `ich9-intel-hda` in order to use the QEMU Intel HDA ICH6 software driver instead of ICH9
      - Note that current `AudioDxe.efi` UEFI sound quality in QEMU is not representative of the quality available on real hardware

#### Debugger Configuration

For simplicitly `efidebug.tool` performs all the necessary GDB or LLDB scripting.
Note, this adds the `reload-uefi` command within the debugger, which you need to run after any new binary loads.

The script will run and attempt to connect with reasonable defaults without additional configuration, but check
`efidebug.tool` header for environment variables to configure your setup. For example, you can use `EFI_DEBUGGER`
variable to force LLDB (`LLDB`) or GDB (`GDB`).

#### GDB Configuration

It is a good idea to use GDB Multiarch in case different debugging architectures are planned to be
used. This can be done in several ways:

- https://www.gnu.org/software/gdb/ — from source
- https://macports.org/ — via MacPorts (`sudo port install gdb +multiarch`)
- Your preferred method here

Once GDB is installed you can use `efidebug.tool` for debugging. In case you do not
want to use `efidebug.tool`, the following set of commands can be used as a reference:

```
$ ggdb /opt/UDK/Build/OpenCorePkg/NOOPT_XCODE5/X64/OpenCorePkg/Debug/GdbSyms/GdbSyms/DEBUG/GdbSyms.dll.dSYM/Contents/Resources/DWARF/GdbSyms.dll

target remote localhost:8864
source /opt/UDK/OpenCorePkg/Debug/Scripts/gdb_uefi.py
set pagination off
reload-uefi
b DebugBreak
```

#### CLANGDWARF

CLANGDWARF toolchain is an LLVM-based toolchain that directly generates
PE/COFF images with DWARF debug information via LLD linker. LLVM 9.0 or
newer with working dead code stripping in LLD is required for this to work
([LLD patches](https://bugs.llvm.org/show_bug.cgi?id=45273)).

For debugging support it may be necessary to set `EFI_SYMBOL_PATH`
environment variable to `:`-separated list of paths with `.debug` files,
for example:

```
export EFI_SYMBOL_PATH="$WORKSPACE/Build/OvmfX64/NOOPT_CLANGPDB/X64:$WORKSPACE/Build/OpenCorePkg/NOOPT_CLANGPDB/X64"
```

The reason for this requirement is fragile `--add-gnudebug-link` option
[implementation in llvm-objcopy](https://bugs.llvm.org/show_bug.cgi?id=45277).
It strips path from the debug file preserving only filename and also does not
update DataDirectory debug entry.

#### IDE Source Level Debugging

Once you have got command line GDB or LLDB source level debugging working, setting up IDE source level debugging (if you prefer to use it)
is a matter of choosing an IDE which already knows about whichever of GDB or LLDB you will be using, and then extracting the relevant config
setup which `efidebug.tool` would have applied for you.

For example, this is a working setup for LLDB debugging in VS Code on macOS:

```
{
    "name": "OC lldb",
    "type": "cppdbg",
    "request": "launch",
    "targetArchitecture": "x64",
    "program": "${workspaceFolder}/Debug/GdbSyms/Bin/X64_XCODE5/GdbSyms.dll",
    "cwd": "${workspaceFolder}/Debug",
    "MIMode": "lldb",
    "setupCommands": [
        {"text": "settings set plugin.process.gdb-remote.target-definition-file Scripts/x86_64_target_definition.py"},
    ],
    "customLaunchSetupCommands": [
        {"text": "gdb-remote localhost:8864"},
        {"text": "target create GdbSyms/Bin/X64_XCODE5/GdbSyms.dll", "ignoreFailures": true},
        {"text": "command script import Scripts/lldb_uefi.py"},
        {"text": "command script add -c lldb_uefi.ReloadUefi reload-uefi"},
        {"text": "reload-uefi"},
    ],
    "launchCompleteCommand": "exec-continue",
    "logging": {
        "engineLogging": false,
        "trace": true,
        "traceResponse": true
    }
}
```

_Note 1_: Debug type `cppdbg` is part of the standard VSCode cpp tools - you do not need to install any other marketplace LLDB tools.

_Note 2_: Step `b DebugBreak` from `efidebug.tool` is ommitted from `customLaunchSetupCommands`, you need to add the breakpoint by
hand in VSCode before launching your first debug session, otherwise VSCode will complain about hitting a breakpoint which it did not set.

#### References

1. https://communities.vmware.com/thread/390128
1. https://wiki.osdev.org/VMware
1. https://github.com/andreiw/andreiw-wip/tree/master/uefi/DebugPkg
