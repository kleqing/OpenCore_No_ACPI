/** @file
  Copyright (C) 2018, vit9696. All rights reserved.
  Copyright (C) 2020, PMheart. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#include "NvramKeyInfo.h"
#include "ocvalidate.h"
#include "OcValidateLib.h"

#include <Library/BaseLib.h>
#include <Library/OcConsoleLib.h>

/**
  Callback function to verify whether one UEFI driver is duplicated in UEFI->Drivers.

  @param[in]  PrimaryDriver    Primary driver to be checked.
  @param[in]  SecondaryDriver  Secondary driver to be checked.

  @retval     TRUE             If PrimaryDriver and SecondaryDriver are duplicated.
**/
STATIC
BOOLEAN
UefiDriverHasDuplication (
  IN  CONST VOID  *PrimaryDriver,
  IN  CONST VOID  *SecondaryDriver
  )
{
  CONST OC_UEFI_DRIVER_ENTRY  *UefiPrimaryDriver;
  CONST OC_UEFI_DRIVER_ENTRY  *UefiSecondaryDriver;
  CONST CHAR8                 *UefiDriverPrimaryString;
  CONST CHAR8                 *UefiDriverSecondaryString;

  UefiPrimaryDriver         = *(CONST OC_UEFI_DRIVER_ENTRY **)PrimaryDriver;
  UefiSecondaryDriver       = *(CONST OC_UEFI_DRIVER_ENTRY **)SecondaryDriver;
  UefiDriverPrimaryString   = OC_BLOB_GET (&UefiPrimaryDriver->Path);
  UefiDriverSecondaryString = OC_BLOB_GET (&UefiSecondaryDriver->Path);

  return StringIsDuplicated ("UEFI->Drivers", UefiDriverPrimaryString, UefiDriverSecondaryString);
}

/**
  Callback function to verify whether one UEFI ReservedMemory entry overlaps the other,
  in terms of Address and Size.

  @param[in]  PrimaryEntry     Primary entry to be checked.
  @param[in]  SecondaryEntry   Secondary entry to be checked.

  @retval     TRUE             If PrimaryEntry and SecondaryEntry have overlapped Address and Size.
**/
STATIC
BOOLEAN
UefiReservedMemoryHasOverlap (
  IN  CONST VOID  *PrimaryEntry,
  IN  CONST VOID  *SecondaryEntry
  )
{
  CONST OC_UEFI_RSVD_ENTRY  *UefiReservedMemoryPrimaryEntry;
  CONST OC_UEFI_RSVD_ENTRY  *UefiReservedMemorySecondaryEntry;
  UINT64                    UefiReservedMemoryPrimaryAddress;
  UINT64                    UefiReservedMemoryPrimarySize;
  UINT64                    UefiReservedMemorySecondaryAddress;
  UINT64                    UefiReservedMemorySecondarySize;

  UefiReservedMemoryPrimaryEntry     = *(CONST OC_UEFI_RSVD_ENTRY **)PrimaryEntry;
  UefiReservedMemorySecondaryEntry   = *(CONST OC_UEFI_RSVD_ENTRY **)SecondaryEntry;
  UefiReservedMemoryPrimaryAddress   = UefiReservedMemoryPrimaryEntry->Address;
  UefiReservedMemoryPrimarySize      = UefiReservedMemoryPrimaryEntry->Size;
  UefiReservedMemorySecondaryAddress = UefiReservedMemorySecondaryEntry->Address;
  UefiReservedMemorySecondarySize    = UefiReservedMemorySecondaryEntry->Size;

  if (!UefiReservedMemoryPrimaryEntry->Enabled || !UefiReservedMemorySecondaryEntry->Enabled) {
    return FALSE;
  }

  if (  (UefiReservedMemoryPrimaryAddress < UefiReservedMemorySecondaryAddress + UefiReservedMemorySecondarySize)
     && (UefiReservedMemorySecondaryAddress < UefiReservedMemoryPrimaryAddress + UefiReservedMemoryPrimarySize))
  {
    DEBUG ((DEBUG_WARN, "UEFI->ReservedMemory: 条目的地址和大小是重叠的 "));
    return TRUE;
  }

  return FALSE;
}

STATIC
BOOLEAN
ValidateReservedMemoryType (
  IN  CONST CHAR8  *Type
  )
{
  UINTN               Index;
  STATIC CONST CHAR8  *AllowedType[] = {
    "Reserved",          "LoaderCode",    "LoaderData",     "BootServiceCode",         "BootServiceData",
    "RuntimeCode",       "RuntimeData",   "Available",      "Persistent",              "UnusableMemory",
    "ACPIReclaimMemory", "ACPIMemoryNVS", "MemoryMappedIO", "MemoryMappedIOPortSpace", "PalCode"
  };

  for (Index = 0; Index < ARRAY_SIZE (AllowedType); ++Index) {
    if (AsciiStrCmp (Type, AllowedType[Index]) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
UINT32
CheckUefiAPFS (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32   ErrorCount;
  BOOLEAN  IsEnableJumpstartEnabled;
  UINT32   ScanPolicy;

  ErrorCount = 0;

  //
  // If FS restrictions is enabled but APFS FS scanning is disabled, it is an error.
  //
  IsEnableJumpstartEnabled = Config->Uefi.Apfs.EnableJumpstart;
  ScanPolicy               = Config->Misc.Security.ScanPolicy;
  if (  IsEnableJumpstartEnabled
     && ((ScanPolicy & OC_SCAN_FILE_SYSTEM_LOCK) != 0)
     && ((ScanPolicy & OC_SCAN_ALLOW_FS_APFS) == 0))
  {
    DEBUG ((DEBUG_WARN, "UEFI->APFS->EnableJumpstart 已启用, 但在 Misc->Security->ScanPolicy 未允许进行APFS扫描!\n"));
    ++ErrorCount;
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiAppleInput (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  CONST CHAR8  *AppleEvent;

  ErrorCount = 0;

  AppleEvent = OC_BLOB_GET (&Config->Uefi.AppleInput.AppleEvent);
  if (  (AsciiStrCmp (AppleEvent, "Auto") != 0)
     && (AsciiStrCmp (AppleEvent, "Builtin") != 0)
     && (AsciiStrCmp (AppleEvent, "OEM") != 0))
  {
    DEBUG ((DEBUG_WARN, "UEFI->AppleInput->AppleEvent 是非法的 (只能是 Auto, Builtin, OEM)!\n"));
    ++ErrorCount;
  }

  if (Config->Uefi.Input.KeySupport && Config->Uefi.AppleInput.CustomDelays) {
    if (  (Config->Uefi.AppleInput.KeyInitialDelay != 0)
       && (Config->Uefi.AppleInput.KeyInitialDelay < Config->Uefi.Input.KeyForgetThreshold))
    {
      DEBUG ((DEBUG_WARN, "在KeySupport模式下启用KeyInitialDelay, 不为零且小于KeyForgetThreshold值 (可能导致不受控制的重复按键); 使用零（0）代替!\n"));
      ++ErrorCount;
    }

    if (Config->Uefi.AppleInput.KeySubsequentDelay < Config->Uefi.Input.KeyForgetThreshold) {
      DEBUG ((DEBUG_WARN, "KeySubsequentDelay在KeySupport模式下启用，并且小于KeyForgetThreshold值 (可能导致不受控制的重复按键); 使用KeyForgetThreshold值或更大的值代替!\n"));
      ++ErrorCount;
    }
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiGain (
  INT8 Gain,
  CHAR8 *GainName,
  INT8 GainAbove, OPTIONAL
  CHAR8   *GainAboveName  OPTIONAL
  )
{
  UINT32  ErrorCount;

  ErrorCount = 0;

  //
  // Cannot check these as they are already truncated to INT8 before we can validate them.
  // TODO: (?) Add check during DEBUG parsing that specified values fit into what they will be cast to.
  //
 #if 0
  if (Gain < -128) {
    DEBUG ((DEBUG_WARN, "UEFI->Audio->%a 必须大于或等于 -128!\n", GainName));
    ++ErrorCount;
  } else if (Gain > 127) {
    DEBUG ((DEBUG_WARN, "UEFI->Audio->%a 必须小于或等于 127!\n", GainName));
    ++ErrorCount;
  }

 #endif

  if ((GainAboveName != NULL) && (Gain > GainAbove)) {
    DEBUG ((DEBUG_WARN, "UEFI->Audio->%a 必须小于或等于 UEFI->Audio->%a!\n", GainName, GainAboveName));
    ++ErrorCount;
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiAudio (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  BOOLEAN      IsAudioSupportEnabled;
  UINT64       AudioOutMask;
  CONST CHAR8  *AsciiAudioDevicePath;
  CONST CHAR8  *AsciiPlayChime;

  ErrorCount = 0;

  IsAudioSupportEnabled = Config->Uefi.Audio.AudioSupport;
  AudioOutMask          = Config->Uefi.Audio.AudioOutMask;
  AsciiAudioDevicePath  = OC_BLOB_GET (&Config->Uefi.Audio.AudioDevice);
  AsciiPlayChime        = OC_BLOB_GET (&Config->Uefi.Audio.PlayChime);
  if (IsAudioSupportEnabled) {
    if (AudioOutMask == 0) {
      DEBUG ((DEBUG_WARN, "UEFI->Audio->AudioOutMask 启用 AudioSupport 时为零, 不会播放任何声音!\n"));
      ++ErrorCount;
    }

    ErrorCount += CheckUefiGain (
                    Config->Uefi.Audio.MaximumGain,
                    "MaximumGain",
                    0,
                    NULL
                    );

    // No operational reason for MinimumAssistGain <= MaximumGain, but is safer to ensure non-deafening sound levels.
    ErrorCount += CheckUefiGain (
                    Config->Uefi.Audio.MinimumAssistGain,
                    "MinimumAssistGain",
                    Config->Uefi.Audio.MaximumGain,
                    "MaximumGain"
                    );

    ErrorCount += CheckUefiGain (
                    Config->Uefi.Audio.MinimumAudibleGain,
                    "MinimumAudibleGain",
                    Config->Uefi.Audio.MinimumAssistGain,
                    "MinimumAssistGain"
                    );

    ErrorCount += CheckUefiGain (
                    Config->Uefi.Audio.MinimumAudibleGain,
                    "MinimumAudibleGain",
                    Config->Uefi.Audio.MaximumGain,
                    "MaximumGain"
                    );

    if (!AsciiDevicePathIsLegal (AsciiAudioDevicePath)) {
      DEBUG ((DEBUG_WARN, "UEFI->Audio->AudioDevice 不正确! 请检查以上信息!\n"));
      ++ErrorCount;
    }

    if (AsciiPlayChime[0] == '\0') {
      DEBUG ((DEBUG_WARN, "UEFI->Audio->PlayChime 启用AudioSupport后不能为空!\n"));
      ++ErrorCount;
    } else if (  (AsciiStrCmp (AsciiPlayChime, "Auto") != 0)
              && (AsciiStrCmp (AsciiPlayChime, "Enabled") != 0)
              && (AsciiStrCmp (AsciiPlayChime, "Disabled") != 0))
    {
      DEBUG ((DEBUG_WARN, "UEFI->Audio->PlayChime 是错误的 (只能是 Auto, Enabled, 或 Disabled)!\n"));
      ++ErrorCount;
    }
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiDrivers (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32                ErrorCount;
  UINT32                Index;
  OC_UEFI_DRIVER_ENTRY  *DriverEntry;
  CONST CHAR8           *Comment;
  CONST CHAR8           *Driver;
  UINTN                 DriverSumSize;
  BOOLEAN               HasOpenRuntimeEfiDriver;
  BOOLEAN               IsOpenRuntimeLoadEarly;
  UINT32                IndexOpenRuntimeEfiDriver;
  BOOLEAN               HasOpenUsbKbDxeEfiDriver;
  UINT32                IndexOpenUsbKbDxeEfiDriver;
  BOOLEAN               HasPs2KeyboardDxeEfiDriver;
  BOOLEAN               IndexPs2KeyboardDxeEfiDriver;
  BOOLEAN               HasHfsEfiDriver;
  UINT32                IndexHfsEfiDriver;
  BOOLEAN               HasAudioDxeEfiDriver;
  UINT32                IndexAudioDxeEfiDriver;
  BOOLEAN               IsRequestBootVarRoutingEnabled;
  BOOLEAN               IsKeySupportEnabled;
  BOOLEAN               IsConnectDriversEnabled;
  BOOLEAN               HasOpenVariableRuntimeDxeEfiDriver;
  UINT32                IndexOpenVariableRuntimeDxeEfiDriver;

  ErrorCount = 0;

  HasOpenRuntimeEfiDriver              = FALSE;
  IndexOpenRuntimeEfiDriver            = 0;
  HasOpenUsbKbDxeEfiDriver             = FALSE;
  IndexOpenUsbKbDxeEfiDriver           = 0;
  HasPs2KeyboardDxeEfiDriver           = FALSE;
  IndexPs2KeyboardDxeEfiDriver         = 0;
  HasHfsEfiDriver                      = FALSE;
  IndexHfsEfiDriver                    = 0;
  HasAudioDxeEfiDriver                 = FALSE;
  IndexAudioDxeEfiDriver               = 0;
  HasOpenVariableRuntimeDxeEfiDriver   = FALSE;
  IndexOpenVariableRuntimeDxeEfiDriver = 0;
  IsOpenRuntimeLoadEarly               = FALSE;
  for (Index = 0; Index < Config->Uefi.Drivers.Count; ++Index) {
    DriverEntry = Config->Uefi.Drivers.Values[Index];
    Comment     = OC_BLOB_GET (&DriverEntry->Comment);
    Driver      = OC_BLOB_GET (&DriverEntry->Path);

    //
    // Check the length of path relative to OC directory.
    //
    DriverSumSize = L_STR_LEN (OPEN_CORE_UEFI_DRIVER_PATH) + AsciiStrSize (Driver);
    if (DriverSumSize > OC_STORAGE_SAFE_PATH_MAX) {
      DEBUG ((
        DEBUG_WARN,
        "UEFI->Drivers[%u] (%u长度) 太长 (不应超过 %u)!\n",
        Index,
        AsciiStrLen (Driver),
        OC_STORAGE_SAFE_PATH_MAX - L_STR_LEN (OPEN_CORE_UEFI_DRIVER_PATH)
        ));
      ++ErrorCount;
    }

    //
    // Sanitise strings.
    //
    if (!AsciiCommentIsLegal (Comment)) {
      DEBUG ((DEBUG_WARN, "UEFI->Drivers[%u]->Comment 包含非法字符!\n", Index));
      ++ErrorCount;
    }

    if (!AsciiUefiDriverIsLegal (Driver, Index)) {
      ++ErrorCount;
      continue;
    }

    if (!DriverEntry->Enabled) {
      continue;
    }

    if (AsciiStrCmp (Driver, "OpenVariableRuntimeDxe.efi") == 0) {
      HasOpenVariableRuntimeDxeEfiDriver   = TRUE;
      IndexOpenVariableRuntimeDxeEfiDriver = Index;

      if (!DriverEntry->LoadEarly) {
        DEBUG ((DEBUG_WARN, "UEFI->Drivers[%u] 处的 OpenVariableRuntimeDxe 必须将 LoadEarly 设置为 TRUE!\n", Index));
        ++ErrorCount;
      }
    }

    //
    // For all drivers but OpenVariableRuntimeDxe.efi and OpenRuntime.efi, LoadEarly must be FALSE.
    //
    if ((AsciiStrCmp (Driver, "OpenVariableRuntimeDxe.efi") != 0) && (AsciiStrCmp (Driver, "OpenRuntime.efi") != 0) && DriverEntry->LoadEarly) {
      DEBUG ((DEBUG_WARN, "UEFI->Drivers[%u] 处的% a 必须将 LoadEarly 设置为 FALSE!\n", Driver, Index));
      ++ErrorCount;
    }

    if (AsciiStrCmp (Driver, "OpenRuntime.efi") == 0) {
      HasOpenRuntimeEfiDriver   = TRUE;
      IndexOpenRuntimeEfiDriver = Index;
      IsOpenRuntimeLoadEarly    = DriverEntry->LoadEarly;
    }

    if (AsciiStrCmp (Driver, "OpenUsbKbDxe.efi") == 0) {
      HasOpenUsbKbDxeEfiDriver   = TRUE;
      IndexOpenUsbKbDxeEfiDriver = Index;
    }

    if (AsciiStrCmp (Driver, "Ps2KeyboardDxe.efi") == 0) {
      HasPs2KeyboardDxeEfiDriver   = TRUE;
      IndexPs2KeyboardDxeEfiDriver = Index;
    }

    //
    // There are several HFS Plus drivers, including HfsPlus, VboxHfs, etc.
    // Here only "hfs" (case-insensitive) is matched.
    //
    if (OcAsciiStriStr (Driver, "hfs") != NULL) {
      HasHfsEfiDriver   = TRUE;
      IndexHfsEfiDriver = Index;
    }

    if (AsciiStrCmp (Driver, "AudioDxe.efi") == 0) {
      HasAudioDxeEfiDriver   = TRUE;
      IndexAudioDxeEfiDriver = Index;
    }
  }

  //
  // Check duplicated Drivers.
  //
  ErrorCount += FindArrayDuplication (
                  Config->Uefi.Drivers.Values,
                  Config->Uefi.Drivers.Count,
                  sizeof (Config->Uefi.Drivers.Values[0]),
                  UefiDriverHasDuplication
                  );

  if (HasOpenRuntimeEfiDriver) {
    if (HasOpenVariableRuntimeDxeEfiDriver) {
      if (!IsOpenRuntimeLoadEarly) {
        DEBUG ((
          DEBUG_WARN,
          "OpenRuntime.efi at UEFI->Drivers[%u] should have its LoadEarly set to TRUE when OpenVariableRuntimeDxe.efi at UEFI->Drivers[%u] is in use!\n",
          IndexOpenRuntimeEfiDriver,
          IndexOpenVariableRuntimeDxeEfiDriver
          ));
        ++ErrorCount;
      }

      if (IndexOpenVariableRuntimeDxeEfiDriver >= IndexOpenRuntimeEfiDriver) {
        DEBUG ((
          DEBUG_WARN,
          "OpenRuntime.efi (目前在 UEFI->Drivers[%u] 处) 应该放在 OpenVariableRuntimeDxe.efi 之后 (目前在 UEFI->Drivers[%u])!\n",
          IndexOpenRuntimeEfiDriver,
          IndexOpenVariableRuntimeDxeEfiDriver
          ));
        ++ErrorCount;
      }
    } else {
      if (IsOpenRuntimeLoadEarly) {
        DEBUG ((
          DEBUG_WARN,
          "UEFI->Drivers[%u] 处的 OpenRuntime.efi 应将其 LoadEarly 设置为 FALSE，除非正在使用 OpenVariableRuntimeDxe.efi!\n",
          IndexOpenRuntimeEfiDriver
          ));
        ++ErrorCount;
      }
    }
  }

  IsRequestBootVarRoutingEnabled = Config->Uefi.Quirks.RequestBootVarRouting;
  if (IsRequestBootVarRoutingEnabled) {
    if (!HasOpenRuntimeEfiDriver) {
      DEBUG ((DEBUG_WARN, "UEFI->Quirks->RequestBootVarRouting已启用, 但是OpenRuntime.efi未在UEFI->Drivers处加载!\n"));
      ++ErrorCount;
    }
  }

  IsKeySupportEnabled = Config->Uefi.Input.KeySupport;
  if (IsKeySupportEnabled) {
    if (HasOpenUsbKbDxeEfiDriver) {
      DEBUG ((DEBUG_WARN, "在UEFI->Drivers[%u]处存在OpenUsbKbDxe.efi 不应该和UEFI->Input->KeySupport一起使用!\n", IndexOpenUsbKbDxeEfiDriver));
      ++ErrorCount;
    }
  } else {
    if (HasPs2KeyboardDxeEfiDriver) {
      DEBUG ((DEBUG_WARN, "UEFI->Input->KeySupport当Ps2KeyboardDxe.efi使用时应该启用!\n"));
      ++ErrorCount;
    }
  }

  if (HasOpenUsbKbDxeEfiDriver && HasPs2KeyboardDxeEfiDriver) {
    DEBUG ((
      DEBUG_WARN,
      "在UEFI->Drivers[%u]处的OpenUsbKbDxe.efi ,和Ps2KeyboardDxe.efi, 不应该一起存在!\n",
      IndexOpenUsbKbDxeEfiDriver,
      IndexPs2KeyboardDxeEfiDriver
      ));
    ++ErrorCount;
  }

  IsConnectDriversEnabled = Config->Uefi.ConnectDrivers;
  if (!IsConnectDriversEnabled) {
    if (HasHfsEfiDriver) {
      DEBUG ((DEBUG_WARN, "HFS文件系统驱动程序在UEFI->Drivers[%u]中加载,但是没有启用UEFI->ConnectDrivers!\n", IndexHfsEfiDriver));
      ++ErrorCount;
    }

    if (HasAudioDxeEfiDriver) {
      DEBUG ((DEBUG_WARN, "AudioDevice.efi 在UEFI->Drivers[%u]中加载,但是没有启用UEFI->ConnectDrivers!\n", IndexAudioDxeEfiDriver));
      ++ErrorCount;
    }
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiInput (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  BOOLEAN      IsPointerSupportEnabled;
  CONST CHAR8  *PointerSupportMode;
  CONST CHAR8  *KeySupportMode;

  ErrorCount = 0;

  IsPointerSupportEnabled = Config->Uefi.Input.PointerSupport;
  PointerSupportMode      = OC_BLOB_GET (&Config->Uefi.Input.PointerSupportMode);
  if (IsPointerSupportEnabled && (AsciiStrCmp (PointerSupportMode, "ASUS") != 0)) {
    DEBUG ((DEBUG_WARN, "UEFI->Input->启用了PointerSupport，但PointerSupportMode不是ASUS!\n"));
    ++ErrorCount;
  }

  KeySupportMode = OC_BLOB_GET (&Config->Uefi.Input.KeySupportMode);
  if (  (AsciiStrCmp (KeySupportMode, "Auto") != 0)
     && (AsciiStrCmp (KeySupportMode, "V1") != 0)
     && (AsciiStrCmp (KeySupportMode, "V2") != 0)
     && (AsciiStrCmp (KeySupportMode, "AMI") != 0))
  {
    DEBUG ((DEBUG_WARN, "UEFI->Input->KeySupportMode不合法 (只能是 Auto, V1, V2, AMI)!\n"));
    ++ErrorCount;
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiOutput (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  CONST CHAR8  *TextRenderer;
  CONST CHAR8  *GopPassThrough;
  BOOLEAN      IsTextRendererSystem;
  BOOLEAN      IsClearScreenOnModeSwitchEnabled;
  BOOLEAN      IsIgnoreTextInGraphicsEnabled;
  BOOLEAN      IsReplaceTabWithSpaceEnabled;
  BOOLEAN      IsSanitiseClearScreenEnabled;
  CONST CHAR8  *ConsoleMode;
  CONST CHAR8  *Resolution;
  UINT32       UserWidth;
  UINT32       UserHeight;
  UINT32       UserBpp;
  BOOLEAN      UserSetMax;
  INT8         UIScale;
  BOOLEAN      HasUefiOutputUIScale;

  ErrorCount           = 0;
  IsTextRendererSystem = FALSE;
  HasUefiOutputUIScale = FALSE;

  //
  // Sanitise strings.
  //
  TextRenderer = OC_BLOB_GET (&Config->Uefi.Output.TextRenderer);
  if (  (AsciiStrCmp (TextRenderer, "BuiltinGraphics") != 0)
     && (AsciiStrCmp (TextRenderer, "BuiltinText") != 0)
     && (AsciiStrCmp (TextRenderer, "SystemGraphics") != 0)
     && (AsciiStrCmp (TextRenderer, "SystemText") != 0)
     && (AsciiStrCmp (TextRenderer, "SystemGeneric") != 0))
  {
    DEBUG ((DEBUG_WARN, "UEFI->Output->TextRenderer是无效的 (只能是BuiltinGraphics, BuiltinText, SystemGraphics, SystemText, 或 SystemGeneric)!\n"));
    ++ErrorCount;
  } else if (AsciiStrnCmp (TextRenderer, "System", L_STR_LEN ("System")) == 0) {
    //
    // Check whether TextRenderer has System prefix.
    //
    IsTextRendererSystem = TRUE;
  }

  if (!IsTextRendererSystem) {
    IsClearScreenOnModeSwitchEnabled = Config->Uefi.Output.ClearScreenOnModeSwitch;
    if (IsClearScreenOnModeSwitchEnabled) {
      DEBUG ((DEBUG_WARN, "UEFI->Output->ClearScreenOnModeSwitch没有在System TextRenderer模式下启用 (当前模式为 %a)!\n", TextRenderer));
      ++ErrorCount;
    }

    IsIgnoreTextInGraphicsEnabled = Config->Uefi.Output.IgnoreTextInGraphics;
    if (IsIgnoreTextInGraphicsEnabled) {
      DEBUG ((DEBUG_WARN, "UEFI->Output->IgnoreTextInGraphics没有在System TextRenderer模式下启用 (当前模式为 %a)!\n", TextRenderer));
      ++ErrorCount;
    }

    IsReplaceTabWithSpaceEnabled = Config->Uefi.Output.ReplaceTabWithSpace;
    if (IsReplaceTabWithSpaceEnabled) {
      DEBUG ((DEBUG_WARN, "UEFI->Output->ReplaceTabWithSpace没有在System TextRenderer模式下启用 (当前模式为 %a)!\n", TextRenderer));
      ++ErrorCount;
    }

    IsSanitiseClearScreenEnabled = Config->Uefi.Output.SanitiseClearScreen;
    if (IsSanitiseClearScreenEnabled) {
      DEBUG ((DEBUG_WARN, "UEFI->Output->SanitiseClearScreen没有在System TextRenderer模式下启用 (当前模式为 %a)!\n", TextRenderer));
      ++ErrorCount;
    }
  }

  GopPassThrough = OC_BLOB_GET (&Config->Uefi.Output.GopPassThrough);
  if (  (AsciiStrCmp (GopPassThrough, "Enabled") != 0)
     && (AsciiStrCmp (GopPassThrough, "Disabled") != 0)
     && (AsciiStrCmp (GopPassThrough, "Apple") != 0))
  {
    DEBUG ((DEBUG_WARN, "UEFI->Output->GopPassThrough 是非法的 (只能是 Enabled, Disabled, Apple)!\n"));
    ++ErrorCount;
  }

  //
  // Parse Output->ConsoleMode by calling OpenCore libraries.
  //
  ConsoleMode = OC_BLOB_GET (&Config->Uefi.Output.ConsoleMode);
  OcParseConsoleMode (
    ConsoleMode,
    &UserWidth,
    &UserHeight,
    &UserSetMax
    );
  if (  (ConsoleMode[0] != '\0')
     && !UserSetMax
     && ((UserWidth == 0) || (UserHeight == 0)))
  {
    DEBUG ((DEBUG_WARN, "UEFI->Output->ConsoleMode不太对, 请查看Configurations.pdf!\n"));
    ++ErrorCount;
  }

  //
  // Parse Output->Resolution by calling OpenCore libraries.
  //
  Resolution = OC_BLOB_GET (&Config->Uefi.Output.Resolution);
  OcParseScreenResolution (
    Resolution,
    &UserWidth,
    &UserHeight,
    &UserBpp,
    &UserSetMax
    );
  if (  (Resolution[0] != '\0')
     && !UserSetMax
     && ((UserWidth == 0) || (UserHeight == 0)))
  {
    DEBUG ((DEBUG_WARN, "UEFI->Output->Resolution不太对, 请查看Configurations.pdf!\n"));
    ++ErrorCount;
  }

  UIScale = Config->Uefi.Output.UIScale;
  if ((UIScale < -1) || (UIScale > 2)) {
    DEBUG ((DEBUG_WARN, "UEFI->Output->UIScale 不太对 (只能介于 -1 和 2 之间)!\n"));
    ++ErrorCount;
  } else if (UIScale != -1) {
    HasUefiOutputUIScale = TRUE;
  }

  if (HasUefiOutputUIScale && mHasNvramUIScale) {
    DEBUG ((DEBUG_WARN, "UIScale 在 NVRAM 和 UEFI->Output 下设置!\n"));
    ++ErrorCount;
  }

  return ErrorCount;
}

//
// FIXME: Just in case this can be of use...
//
STATIC
UINT32
CheckUefiQuirks (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32  ErrorCount;
  INT8    ResizeGpuBars;

  ErrorCount    = 0;
  ResizeGpuBars = Config->Uefi.Quirks.ResizeGpuBars;

  if ((ResizeGpuBars < -1) || (ResizeGpuBars > 19)) {
    DEBUG ((DEBUG_WARN, "UEFI->Quirks->ResizeGpuBars 不太对 (只能在 -1 到 19 之间)!\n"));
    ++ErrorCount;
  }

  return ErrorCount;
}

STATIC
UINT32
CheckUefiReservedMemory (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       Index;
  CONST CHAR8  *AsciiReservedMemoryType;
  UINT64       ReservedMemoryAddress;
  UINT64       ReservedMemorySize;

  ErrorCount = 0;

  //
  // Validate ReservedMemory[N].
  //
  for (Index = 0; Index < Config->Uefi.ReservedMemory.Count; ++Index) {
    AsciiReservedMemoryType = OC_BLOB_GET (&Config->Uefi.ReservedMemory.Values[Index]->Type);
    ReservedMemoryAddress   = Config->Uefi.ReservedMemory.Values[Index]->Address;
    ReservedMemorySize      = Config->Uefi.ReservedMemory.Values[Index]->Size;

    if (!ValidateReservedMemoryType (AsciiReservedMemoryType)) {
      DEBUG ((DEBUG_WARN, "UEFI->ReservedMemory[%u]->类型不对!\n", Index));
      ++ErrorCount;
    }

    if (ReservedMemoryAddress % EFI_PAGE_SIZE != 0) {
      DEBUG ((DEBUG_WARN, "UEFI->ReservedMemory[%u]->Address (%Lu) 不能除以页面大小!\n", Index, ReservedMemoryAddress));
      ++ErrorCount;
    }

    if (ReservedMemorySize == 0ULL) {
      DEBUG ((DEBUG_WARN, "UEFI->ReservedMemory[%u]->Size 不能为0!\n", Index));
      ++ErrorCount;
    } else if (ReservedMemorySize % EFI_PAGE_SIZE != 0) {
      DEBUG ((DEBUG_WARN, "UEFI->ReservedMemory[%u]->Size (%Lu) 不能除以页面大小!\n", Index, ReservedMemorySize));
      ++ErrorCount;
    }
  }

  //
  // Now overlapping check amongst Address and Size.
  //
  ErrorCount += FindArrayDuplication (
                  Config->Uefi.ReservedMemory.Values,
                  Config->Uefi.ReservedMemory.Count,
                  sizeof (Config->Uefi.ReservedMemory.Values[0]),
                  UefiReservedMemoryHasOverlap
                  );

  return ErrorCount;
}

UINT32
CheckUefi (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32               ErrorCount;
  UINTN                Index;
  STATIC CONFIG_CHECK  UefiCheckers[] = {
    &CheckUefiAPFS,
    &CheckUefiAppleInput,
    &CheckUefiAudio,
    &CheckUefiDrivers,
    &CheckUefiInput,
    &CheckUefiOutput,
    &CheckUefiQuirks,
    &CheckUefiReservedMemory
  };

  DEBUG ((DEBUG_VERBOSE, "config loaded into %a!\n", __func__));

  ErrorCount = 0;

  for (Index = 0; Index < ARRAY_SIZE (UefiCheckers); ++Index) {
    ErrorCount += UefiCheckers[Index](Config);
  }

  return ReportError (__func__, ErrorCount);
}
