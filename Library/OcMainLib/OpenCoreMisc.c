/** @file
  OpenCore driver.

Copyright (c) 2019, vit9696. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <IndustryStandard/AppleCsrConfig.h>

#include <Library/OcMainLib.h>

#include <Guid/AppleVariable.h>
#include <Guid/OcVariable.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OcAcpiLib.h>
#include <Library/OcAppleBootPolicyLib.h>
#include <Library/OcAudioLib.h>
#include <Library/OcBootManagementLib.h>
#include <Library/OcConsoleLib.h>
#include <Library/OcCpuLib.h>
#include <Library/OcDebugLogLib.h>
#include <Library/OcDeviceMiscLib.h>
#include <Library/OcLogAggregatorLib.h>
#include <Library/OcSmbiosLib.h>
#include <Library/OcStringLib.h>
#include <Library/OcVariableLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/OcInterface.h>

STATIC
VOID
OcStoreLoadPath (
  IN EFI_DEVICE_PATH_PROTOCOL  *LoadPath OPTIONAL
  )
{
  EFI_STATUS  Status;
  CHAR16      *DevicePath;
  CHAR8       OutPath[256];

  if (LoadPath != NULL) {
    DevicePath = ConvertDevicePathToText (LoadPath, FALSE, FALSE);
    if (DevicePath != NULL) {
      AsciiSPrint (OutPath, sizeof (OutPath), "%s", DevicePath);
      FreePool (DevicePath);
    } else {
      LoadPath = NULL;
    }
  }

  if (LoadPath == NULL) {
    AsciiSPrint (OutPath, sizeof (OutPath), "Unknown");
  }

  Status = OcSetSystemVariable (
             OC_LOG_VARIABLE_PATH,
             OPEN_CORE_NVRAM_ATTR,
             AsciiStrSize (OutPath),
             OutPath,
             NULL
             );

  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_WARN : DEBUG_INFO,
    "OC: Setting NVRAM %g:%s = %a - %r\n",
    &gOcVendorVariableGuid,
    OC_LOG_VARIABLE_PATH,
    OutPath,
    Status
    ));
}

STATIC
EFI_STATUS
ProduceDebugReport (
  IN EFI_HANDLE  VolumeHandle
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL  *SysReport;
  EFI_FILE_PROTOCOL  *SubReport;
  OC_CPU_INFO        CpuInfo;

  OcCpuScanProcessor (&CpuInfo);

  if (VolumeHandle != NULL) {
    Fs = OcLocateRootVolume (VolumeHandle, NULL);
  } else {
    Fs = NULL;
  }

  if (Fs == NULL) {
    Status = OcFindWritableFileSystem (&Fs);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "OC: No usable filesystem for report - %r\n", Status));
      return EFI_NOT_FOUND;
    }
  }

  Status = OcSafeFileOpen (
             Fs,
             &SysReport,
             L"SysReport",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
             EFI_FILE_DIRECTORY
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Report is already created, skipping\n"));
    SysReport->Close (SysReport);
    Fs->Close (Fs);
    return EFI_ALREADY_STARTED;
  }

  Status = OcSafeFileOpen (
             Fs,
             &SysReport,
             L"SysReport",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Cannot create SysReport - %r\n", Status));
    Fs->Close (Fs);
    return Status;
  }

  Status = OcSafeFileOpen (
             SysReport,
             &SubReport,
             L"ACPI",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Dumping ACPI for report...\n"));
    Status = AcpiDumpTables (SubReport);
    SubReport->Close (SubReport);
  }

  DEBUG ((DEBUG_INFO, "OC: ACPI dumping - %r\n", Status));

  Status = OcSafeFileOpen (
             SysReport,
             &SubReport,
             L"SMBIOS",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Dumping SMBIOS for report...\n"));
    Status = OcSmbiosDump (SubReport);
    SubReport->Close (SubReport);
  }

  DEBUG ((DEBUG_INFO, "OC: SMBIOS dumping - %r\n", Status));

  Status = OcSafeFileOpen (
             SysReport,
             &SubReport,
             L"Audio",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Dumping audio for report...\n"));
    Status = OcAudioDump (SubReport);
    SubReport->Close (SubReport);
  }

  DEBUG ((DEBUG_INFO, "OC: Audio dumping - %r\n", Status));

  Status = OcSafeFileOpen (
             SysReport,
             &SubReport,
             L"CPU",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Dumping CPUInfo for report...\n"));
    Status = OcCpuInfoDump (&CpuInfo, SubReport);
    SubReport->Close (SubReport);
  }

  DEBUG ((DEBUG_INFO, "OC: CPUInfo dumping - %r\n", Status));

  Status = OcSafeFileOpen (
             SysReport,
             &SubReport,
             L"PCI",
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Dumping PCIInfo for report...\n"));
    Status = OcPciInfoDump (SubReport);
    SubReport->Close (SubReport);
  }

  DEBUG ((DEBUG_INFO, "OC: PCIInfo dumping - %r\n", Status));

  SysReport->Close (SysReport);
  Fs->Close (Fs);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
OcToolLoadEntry (
  IN  OC_STORAGE_CONTEXT        *Storage,
  IN  OC_BOOT_ENTRY             *ChosenEntry,
  OUT VOID                      **Data,
  OUT UINT32                    *DataSize,
  OUT EFI_DEVICE_PATH_PROTOCOL  **DevicePath,
  OUT EFI_HANDLE                *StorageHandle,
  OUT EFI_DEVICE_PATH_PROTOCOL  **StoragePath
  )
{
  EFI_STATUS  Status;
  CHAR16      ToolPath[OC_STORAGE_SAFE_PATH_MAX];

  Status = OcUnicodeSafeSPrint (
             ToolPath,
             sizeof (ToolPath),
             OPEN_CORE_TOOL_PATH "%s",
             ChosenEntry->PathName
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "OC: Tool %s%s does not fit path!\n",
      OPEN_CORE_TOOL_PATH,
      ChosenEntry->PathName
      ));
    return EFI_NOT_FOUND;
  }

  *Data = OcStorageReadFileUnicode (
            Storage,
            ToolPath,
            DataSize
            );
  if (*Data == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "OC: Tool %s cannot be found!\n",
      ToolPath
      ));
    return EFI_NOT_FOUND;
  }

  Status = OcStorageGetInfo (
             Storage,
             ToolPath,
             DevicePath,
             StorageHandle,
             StoragePath,
             ChosenEntry->ExposeDevicePath
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Returning tool %s\n", ToolPath));
    DebugPrintDevicePath (DEBUG_INFO, "OC: Tool path", *DevicePath);
    DebugPrintDevicePath (DEBUG_INFO, "OC: Storage path", *StoragePath);
  }

  return EFI_SUCCESS;
}

STATIC
VOID
SavePanicLog (
  IN OC_STORAGE_CONTEXT  *Storage
  )
{
  EFI_STATUS         Status;
  VOID               *PanicLog;
  EFI_FILE_PROTOCOL  *RootFs;
  UINT32             PanicLogSize;
  EFI_TIME           PanicLogDate;
  CHAR16             PanicLogName[32];

  PanicLog = OcReadApplePanicLog (&PanicLogSize);
  if (PanicLog != NULL) {
    Status = gRT->GetTime (&PanicLogDate, NULL);
    if (EFI_ERROR (Status)) {
      ZeroMem (&PanicLogDate, sizeof (PanicLogDate));
    }

    UnicodeSPrint (
      PanicLogName,
      sizeof (PanicLogName),
      L"panic-%04u-%02u-%02u-%02u%02u%02u.txt",
      (UINT32)PanicLogDate.Year,
      (UINT32)PanicLogDate.Month,
      (UINT32)PanicLogDate.Day,
      (UINT32)PanicLogDate.Hour,
      (UINT32)PanicLogDate.Minute,
      (UINT32)PanicLogDate.Second
      );

    Status = Storage->FileSystem->OpenVolume (
                                    Storage->FileSystem,
                                    &RootFs
                                    );
    if (!EFI_ERROR (Status)) {
      Status = OcSetFileData (RootFs, PanicLogName, PanicLog, PanicLogSize);
      RootFs->Close (RootFs);
    }

    DEBUG ((DEBUG_INFO, "OC: Saving %u byte panic log %s - %r\n", PanicLogSize, PanicLogName, Status));
    FreePool (PanicLog);
  } else {
    DEBUG ((DEBUG_INFO, "OC: Panic log does not exist\n"));
  }
}

CONST CHAR8 *
OcMiscGetVersionString (
  VOID
  )
{
  UINT32  Month;

  /**
    Force the assertions in case we forget about them.
  **/
  STATIC_ASSERT (
    L_STR_LEN (OPEN_CORE_VERSION) == 5,
    "OPEN_CORE_VERSION must follow X.Y.Z format, where X.Y.Z are single digits."
    );

  STATIC_ASSERT (
    L_STR_LEN (OPEN_CORE_TARGET) == 3,
    "OPEN_CORE_TARGET must follow XYZ format, where XYZ is build target."
    );

  STATIC CHAR8  mOpenCoreVersion[] = {
    /* [2]:[0]    = */ OPEN_CORE_TARGET
    /* [3]        = */ "-"
    /* [6]:[4]    = */ "XXX"
    /* [7]        = */ "-"
    /* [12]:[8]   = */ "YYYY-"
    /* [15]:[13]  = */ "MM-"
    /* [17]:[16]  = */ "DD"
    " | MOD By BTWISE | WeChat:15242609 QQ:15242609"
  };

  STATIC BOOLEAN  mOpenCoreVersionReady;

  if (!mOpenCoreVersionReady) {
    mOpenCoreVersion[4] = OPEN_CORE_VERSION[0];
    mOpenCoreVersion[5] = OPEN_CORE_VERSION[2];
    mOpenCoreVersion[6] = OPEN_CORE_VERSION[4];

    mOpenCoreVersion[8]  = __DATE__[7];
    mOpenCoreVersion[9]  = __DATE__[8];
    mOpenCoreVersion[10] = __DATE__[9];
    mOpenCoreVersion[11] = __DATE__[10];

    Month =
      (__DATE__[0] == 'J' && __DATE__[1] == 'a' && __DATE__[2] == 'n') ?  1 :
      (__DATE__[0] == 'F' && __DATE__[1] == 'e' && __DATE__[2] == 'b') ?  2 :
      (__DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'r') ?  3 :
      (__DATE__[0] == 'A' && __DATE__[1] == 'p' && __DATE__[2] == 'r') ?  4 :
      (__DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'y') ?  5 :
      (__DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'n') ?  6 :
      (__DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'l') ?  7 :
      (__DATE__[0] == 'A' && __DATE__[1] == 'u' && __DATE__[2] == 'g') ?  8 :
      (__DATE__[0] == 'S' && __DATE__[1] == 'e' && __DATE__[2] == 'p') ?  9 :
      (__DATE__[0] == 'O' && __DATE__[1] == 'c' && __DATE__[2] == 't') ? 10 :
      (__DATE__[0] == 'N' && __DATE__[1] == 'o' && __DATE__[2] == 'v') ? 11 :
      (__DATE__[0] == 'D' && __DATE__[1] == 'e' && __DATE__[2] == 'c') ? 12 : 0;

    mOpenCoreVersion[13] = Month < 10 ? '0' : '1';
    mOpenCoreVersion[14] = '0' + (Month % 10);
    mOpenCoreVersion[16] = __DATE__[4] >= '0' ? __DATE__[4] : '0';
    mOpenCoreVersion[17] = __DATE__[5];

    mOpenCoreVersionReady = TRUE;
  }

  return mOpenCoreVersion;
}

//构造一个欢迎字符串函数
CONST CHAR8 *
OcWelcomeString (
  VOID
  )
{

  STATIC CHAR8 mWelcome[] = {
    "Welcome To Use  MOD-OC"
  };

  STATIC BOOLEAN mWelcomeReady;

  if (!mWelcomeReady) {
    mWelcomeReady = TRUE;
  }

  return mWelcome;
}

//构造结束
EFI_STATUS
OcMiscEarlyInit (
  IN  OC_STORAGE_CONTEXT  *Storage,
  OUT OC_GLOBAL_CONFIG    *Config,
  IN  OC_RSA_PUBLIC_KEY   *VaultKey  OPTIONAL
  )
{
  EFI_STATUS      Status;
  CHAR8           *ConfigData;
  UINT32          ConfigDataSize;
  EFI_TIME        BootTime;
  CONST CHAR8     *AsciiVault;
  OCS_VAULT_MODE  Vault;
  UINTN           PciDeviceInfoSize;

  ConfigData = OcStorageReadFileUnicode (
                 Storage,
                 OPEN_CORE_CONFIG_PATH,
                 &ConfigDataSize
                 );

  if (ConfigData != NULL) {
    DEBUG ((DEBUG_INFO, "OC: Loaded configuration of %u bytes\n", ConfigDataSize));

    Status = OcConfigurationInit (Config, ConfigData, ConfigDataSize, NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "OC: Failed to parse configuration!\n"));
      CpuDeadLoop ();
      return EFI_UNSUPPORTED; ///< Should be unreachable.
    }

    FreePool (ConfigData);
  } else {
    DEBUG ((DEBUG_ERROR, "OC: Failed to load configuration!\n"));
    CpuDeadLoop ();
    return EFI_UNSUPPORTED; ///< Should be unreachable.
  }

  OcLoadDrivers (Storage, Config, NULL, TRUE);

  OcVariableInit (Config->Uefi.Quirks.ForceOcWriteFlash);

  AsciiVault = OC_BLOB_GET (&Config->Misc.Security.Vault);
  if (AsciiStrCmp (AsciiVault, "Secure") == 0) {
    Vault = OcsVaultSecure;
  } else if (AsciiStrCmp (AsciiVault, "Optional") == 0) {
    Vault = OcsVaultOptional;
  } else if (AsciiStrCmp (AsciiVault, "Basic") == 0) {
    Vault = OcsVaultBasic;
  } else {
    DEBUG ((DEBUG_ERROR, "OC: Invalid Vault mode: %a\n", AsciiVault));
    CpuDeadLoop ();
    return EFI_UNSUPPORTED; ///< Should be unreachable.
  }

  //
  // Sanity check that the configuration is adequate.
  //
  if (!Storage->HasVault && (Vault >= OcsVaultBasic)) {
    DEBUG ((DEBUG_ERROR, "OC: Configuration requires vault but no vault provided!\n"));
    CpuDeadLoop ();
    return EFI_SECURITY_VIOLATION; ///< Should be unreachable.
  }

  if ((VaultKey == NULL) && (Vault >= OcsVaultSecure)) {
    DEBUG ((DEBUG_ERROR, "OC: Configuration requires signed vault but no public key provided!\n"));
    CpuDeadLoop ();
    return EFI_SECURITY_VIOLATION; ///< Should be unreachable.
  }

  DEBUG ((DEBUG_INFO, "OC: Watchdog status is %d\n", Config->Misc.Debug.DisableWatchDog == FALSE));

  if (Config->Misc.Debug.DisableWatchDog) {
    //
    // boot.efi kills watchdog only in FV2 UI.
    //
    gBS->SetWatchdogTimer (0, 0, 0, NULL);
  }

  if (Config->Misc.Serial.Override) {
    //
    // Validate the size of PciDeviceInfo. Abort on error.
    //
    PciDeviceInfoSize = Config->Misc.Serial.Custom.PciDeviceInfo.Size;
    if (PciDeviceInfoSize > OC_SERIAL_PCI_DEVICE_INFO_MAX_SIZE) {
      DEBUG ((DEBUG_INFO, "OC: Aborting overriding serial port properties with borked PciDeviceInfo size %u\n", PciDeviceInfoSize));
    } else {
      PatchPcdSetPtr (PcdSerialPciDeviceInfo, &PciDeviceInfoSize, OC_BLOB_GET (&Config->Misc.Serial.Custom.PciDeviceInfo));
      PatchPcdSet8 (PcdSerialRegisterAccessWidth, Config->Misc.Serial.Custom.RegisterAccessWidth);
      PatchPcdSetBool (PcdSerialUseMmio, Config->Misc.Serial.Custom.UseMmio);
      PatchPcdSetBool (PcdSerialUseHardwareFlowControl, Config->Misc.Serial.Custom.UseHardwareFlowControl);
      PatchPcdSetBool (PcdSerialDetectCable, Config->Misc.Serial.Custom.DetectCable);
      PatchPcdSet64 (PcdSerialRegisterBase, Config->Misc.Serial.Custom.RegisterBase);
      PatchPcdSet32 (PcdSerialBaudRate, Config->Misc.Serial.Custom.BaudRate);
      PatchPcdSet8 (PcdSerialLineControl, Config->Misc.Serial.Custom.LineControl);
      PatchPcdSet8 (PcdSerialFifoControl, Config->Misc.Serial.Custom.FifoControl);
      PatchPcdSet32 (PcdSerialClockRate, Config->Misc.Serial.Custom.ClockRate);
      PatchPcdSet32 (PcdSerialExtendedTxFifoSize, Config->Misc.Serial.Custom.ExtendedTxFifoSize);
      PatchPcdSet32 (PcdSerialRegisterStride, Config->Misc.Serial.Custom.RegisterStride);
    }
  }

  if (Config->Misc.Serial.Init) {
    SerialPortInitialize ();
  }

  OcConfigureLogProtocol (
    Config->Misc.Debug.Target,
    OC_BLOB_GET (&Config->Misc.Debug.LogModules),
    Config->Misc.Debug.DisplayDelay,
    (UINTN)Config->Misc.Debug.DisplayLevel,
    (UINTN)Config->Misc.Security.HaltLevel,
    OPEN_CORE_LOG_PREFIX_PATH,
    Storage->FileSystem
    );

  DEBUG ((
    DEBUG_INFO,
    "OC: OpenCore %a is loading in %a mode (%d/%d)...\n",
    OcMiscGetVersionString (),
    OcWelcomeString (),
    AsciiVault,
    Storage->HasVault,
    VaultKey != NULL
    ));

  Status = gRT->GetTime (&BootTime, NULL);
  if (!EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_INFO,
      "OC: Boot timestamp - %04u.%02u.%02u %02u:%02u:%02u\n",
      BootTime.Year,
      BootTime.Month,
      BootTime.Day,
      BootTime.Hour,
      BootTime.Minute,
      BootTime.Second
      ));
  } else {
    DEBUG ((
      DEBUG_INFO,
      "OC: Boot timestamp - %r\n",
      Status
      ));
  }

  return EFI_SUCCESS;
}

/**
  Generates bootstrap path according to the BootProtect mode.

  @param[in]  RootPath      Root load path.
  @param[in]  LauncherPath  Launcher path to write, optional.
  @param[out] MatchSuffix   Match suffix to optimise lookup.

  @returns  BootProtect bitmask.
**/
STATIC
CHAR16 *
BuildLauncherPath (
  IN   CONST CHAR16  *RootPath,
  IN   CONST CHAR8   *LauncherPath,
  OUT  CONST CHAR16  **MatchSuffix
  )
{
  CHAR16  *BootstrapPath;
  UINTN   BootstrapSize;

  //
  // MatchSuffix allows us to reduce option duplication when switching between
  // OpenCore versions. Using OpenCore.efi (OPEN_CORE_APP_PATH) will overwrite
  // any option with this path (e.g. OC\OpenCore.efi and OC2\OpenCore.efi).
  // For custom paths no deduplication happens.
  //
  if (AsciiStrCmp (LauncherPath, "Default") == 0) {
    BootstrapSize = StrSize (RootPath) + L_STR_SIZE (OPEN_CORE_APP_PATH);
    BootstrapPath = AllocatePool (BootstrapSize);
    if (BootstrapPath == NULL) {
      return NULL;
    }

    UnicodeSPrint (BootstrapPath, BootstrapSize, L"%s\\%s", RootPath, OPEN_CORE_APP_PATH);
    *MatchSuffix = OPEN_CORE_APP_PATH;
  } else {
    BootstrapPath = AsciiStrCopyToUnicode (LauncherPath, 0);
    if (BootstrapPath == NULL) {
      return NULL;
    }

    *MatchSuffix = BootstrapPath;
  }

  return BootstrapPath;
}

VOID
OcMiscMiddleInit (
  IN  OC_STORAGE_CONTEXT        *Storage,
  IN  OC_GLOBAL_CONFIG          *Config,
  IN  CONST CHAR16              *RootPath,
  IN  EFI_DEVICE_PATH_PROTOCOL  *LoadPath,
  IN  EFI_HANDLE                StorageHandle,
  OUT UINT8                     *Signature  OPTIONAL
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;
  CONST CHAR8                      *LauncherOption;
  CONST CHAR8                      *LauncherPath;
  CHAR16                           *FullLauncherPath;
  CONST CHAR16                     *MatchSuffix;
  VOID                             *LauncherData;
  UINT32                           LauncherSize;
  UINT32                           BootProtectFlag;
  BOOLEAN                          HasFullLauncher;
  BOOLEAN                          HasShortLauncher;
  BOOLEAN                          HasSystemLauncher;

  if ((Config->Misc.Security.ExposeSensitiveData & OCS_EXPOSE_BOOT_PATH) != 0) {
    OcStoreLoadPath (LoadPath);
  }

  BootProtectFlag = 0;
  LauncherOption  = OC_BLOB_GET (&Config->Misc.Boot.LauncherOption);
  LauncherPath    = OC_BLOB_GET (&Config->Misc.Boot.LauncherPath);
  DEBUG ((
    DEBUG_INFO,
    "OC: StorageHandle %p with %a LauncherOption pointing to %a\n",
    StorageHandle,
    LauncherOption,
    LauncherPath
    ));

  //
  // Full-form paths cause entry duplication on e.g. HP 15-ab237ne, InsydeH2O.
  //
  HasFullLauncher   = AsciiStrCmp (LauncherOption, "Full") == 0;
  HasShortLauncher  = !HasFullLauncher && AsciiStrCmp (LauncherOption, "Short") == 0;
  HasSystemLauncher = !HasFullLauncher && !HasShortLauncher && AsciiStrCmp (LauncherOption, "System") == 0;
  if (!HasFullLauncher && !HasShortLauncher && !HasSystemLauncher) {
    LauncherPath = "Default";
  }

  FullLauncherPath = BuildLauncherPath (RootPath, LauncherPath, &MatchSuffix);
  if (FullLauncherPath != NULL) {
    if (HasFullLauncher || HasShortLauncher) {
      OcRegisterBootstrapBootOption (
        L"OpenCore",
        StorageHandle,
        FullLauncherPath,
        HasShortLauncher,
        MatchSuffix,
        StrLen (MatchSuffix)
        );
      BootProtectFlag = OC_BOOT_PROTECT_VARIABLE_BOOTSTRAP;
    }

    //
    // Note: This technically is a TOCTOU, but no Macs support Secure Boot with OC anyway.
    //
    if (Signature != NULL) {
      Status = gBS->HandleProtocol (
                      StorageHandle,
                      &gEfiSimpleFileSystemProtocolGuid,
                      (VOID **)&FileSystem
                      );
      if (!EFI_ERROR (Status)) {
        LauncherData = OcReadFile (FileSystem, FullLauncherPath, &LauncherSize, BASE_32MB);
        if (LauncherData != NULL) {
          Sha1 (Signature, LauncherData, LauncherSize);
          DEBUG ((
            DEBUG_INFO,
            "OC: Launcher %s signature is %02X%02X%02X%02X\n",
            FullLauncherPath,
            Signature[0],
            Signature[1],
            Signature[2],
            Signature[3]
            ));
          FreePool (LauncherData);
        }
      }
    }

    FreePool (FullLauncherPath);
  }

  //
  // Inform about boot protection.
  //
  OcSetSystemVariable (
    OC_BOOT_PROTECT_VARIABLE_NAME,
    OPEN_CORE_INT_NVRAM_ATTR,
    sizeof (BootProtectFlag),
    &BootProtectFlag,
    NULL
    );
}

EFI_STATUS
OcMiscLateInit (
  IN  OC_STORAGE_CONTEXT  *Storage,
  IN  OC_GLOBAL_CONFIG    *Config
  )
{
  EFI_STATUS   HibernateStatus;
  CONST CHAR8  *HibernateMode;
  UINT32       HibernateMask;

  HibernateMode = OC_BLOB_GET (&Config->Misc.Boot.HibernateMode);

  if (AsciiStrCmp (HibernateMode, "None") == 0) {
    HibernateMask = HIBERNATE_MODE_NONE;
  } else if (AsciiStrCmp (HibernateMode, "Auto") == 0) {
    HibernateMask = HIBERNATE_MODE_RTC | HIBERNATE_MODE_NVRAM;
  } else if (AsciiStrCmp (HibernateMode, "RTC") == 0) {
    HibernateMask = HIBERNATE_MODE_RTC;
  } else if (AsciiStrCmp (HibernateMode, "NVRAM") == 0) {
    HibernateMask = HIBERNATE_MODE_NVRAM;
  } else {
    DEBUG ((DEBUG_INFO, "OC: Invalid HibernateMode: %a\n", HibernateMode));
    HibernateMask = HIBERNATE_MODE_NONE;
  }

  DEBUG ((DEBUG_INFO, "OC: Translated HibernateMode %a to %u\n", HibernateMode, HibernateMask));

  HibernateStatus = OcActivateHibernateWake (HibernateMask);
  DEBUG ((
    DEBUG_INFO,
    "OC: Hibernation activation - %r, hibernation wake - %a\n",
    HibernateStatus,
    OcIsAppleHibernateWake () ? "yes" : "no"
    ));

  if (Config->Misc.Debug.ApplePanic) {
    SavePanicLog (Storage);
  }

  OcAppleDebugLogConfigure (Config->Misc.Debug.AppleDebug);

  return EFI_SUCCESS;
}

VOID
OcMiscLoadSystemReport (
  IN  OC_GLOBAL_CONFIG  *Config,
  IN  EFI_HANDLE        LoadHandle OPTIONAL
  )
{
  if ((LoadHandle != NULL) && Config->Misc.Debug.SysReport) {
    ProduceDebugReport (LoadHandle);
  }
}

VOID
OcMiscBoot (
  IN  OC_STORAGE_CONTEXT    *Storage,
  IN  OC_GLOBAL_CONFIG      *Config,
  IN  OC_PRIVILEGE_CONTEXT  *Privilege OPTIONAL,
  IN  OC_IMAGE_START        StartImage,
  IN  BOOLEAN               CustomBootGuid,
  IN  EFI_HANDLE            LoadHandle
  )
{
  EFI_STATUS              Status;
  OC_PICKER_CONTEXT       *Context;
  OC_PICKER_MODE          PickerMode;
  OC_DMG_LOADING_SUPPORT  DmgLoading;
  UINTN                   ContextSize;
  UINT32                  Index;
  UINT32                  EntryIndex;
  OC_INTERFACE_PROTOCOL   *Interface;
  UINTN                   BlessOverrideSize;
  CHAR16                  **BlessOverride;
  CONST CHAR8             *AsciiPicker;
  CONST CHAR8             *AsciiPickerVariant;
  CONST CHAR8             *AsciiDmg;

  AsciiPicker = OC_BLOB_GET (&Config->Misc.Boot.PickerMode);

  if (AsciiStrCmp (AsciiPicker, "Builtin") == 0) {
    PickerMode = OcPickerModeBuiltin;
  } else if (AsciiStrCmp (AsciiPicker, "External") == 0) {
    PickerMode = OcPickerModeExternal;
  } else if (AsciiStrCmp (AsciiPicker, "Apple") == 0) {
    PickerMode = OcPickerModeApple;
  } else {
    DEBUG ((DEBUG_WARN, "OC: Unknown PickerMode: %a, using builtin\n", AsciiPicker));
    PickerMode = OcPickerModeBuiltin;
  }

  AsciiPickerVariant = OC_BLOB_GET (&Config->Misc.Boot.PickerVariant);

  AsciiDmg = OC_BLOB_GET (&Config->Misc.Security.DmgLoading);

  if (AsciiStrCmp (AsciiDmg, "Disabled") == 0) {
    DmgLoading = OcDmgLoadingDisabled;
  } else if (AsciiStrCmp (AsciiDmg, "Any") == 0) {
    DmgLoading = OcDmgLoadingAnyImage;
  } else if (AsciiStrCmp (AsciiDmg, "Signed") == 0) {
    DmgLoading = OcDmgLoadingAppleSigned;
  } else {
    DEBUG ((DEBUG_WARN, "OC: Unknown DmgLoading: %a, using Signed\n", AsciiDmg));
    DmgLoading = OcDmgLoadingAppleSigned;
  }

  //
  // Do not use our boot picker unless asked.
  //
  if (PickerMode == OcPickerModeExternal) {
    DEBUG ((DEBUG_INFO, "OC: Handing off to external boot controller\n"));

    Status = gBS->LocateProtocol (
                    &gOcInterfaceProtocolGuid,
                    NULL,
                    (VOID **)&Interface
                    );
    if (!EFI_ERROR (Status)) {
      if (Interface->Revision != OC_INTERFACE_REVISION) {
        DEBUG ((
          DEBUG_INFO,
          "OC: Incompatible external GUI protocol - %u vs %u\n",
          Interface->Revision,
          OC_INTERFACE_REVISION
          ));
        Interface = NULL;
      }
    } else {
      DEBUG ((DEBUG_INFO, "OC: Missing external GUI protocol - %r\n", Status));
      Interface = NULL;
    }
  } else {
    Interface = NULL;
  }

  //
  // Due to the file size and sanity guarantees OcXmlLib makes,
  // adding Counts cannot overflow.
  //
  if (!OcOverflowMulAddUN (
         sizeof (OC_PICKER_ENTRY),
         Config->Misc.Entries.Count + Config->Misc.Tools.Count,
         sizeof (OC_PICKER_CONTEXT),
         &ContextSize
         ))
  {
    Context = AllocateZeroPool (ContextSize);
  } else {
    Context = NULL;
  }

  if (Context == NULL) {
    DEBUG ((DEBUG_ERROR, "OC: Failed to allocate boot picker context!\n"));
    return;
  }

  if (Config->Misc.BlessOverride.Count > 0) {
    if (!OcOverflowMulUN (
           Config->Misc.BlessOverride.Count,
           sizeof (*BlessOverride),
           &BlessOverrideSize
           ))
    {
      BlessOverride = AllocateZeroPool (BlessOverrideSize);
    } else {
      BlessOverride = NULL;
    }

    if (BlessOverride == NULL) {
      FreePool (Context);
      DEBUG ((DEBUG_ERROR, "OC: Failed to allocate bless overrides!\n"));
      return;
    }

    for (Index = 0; Index < Config->Misc.BlessOverride.Count; ++Index) {
      BlessOverride[Index] = AsciiStrCopyToUnicode (
                               OC_BLOB_GET (
                                 Config->Misc.BlessOverride.Values[Index]
                                 ),
                               0
                               );
      if (BlessOverride[Index] == NULL) {
        for (EntryIndex = 0; EntryIndex < Index; ++EntryIndex) {
          FreePool (BlessOverride[EntryIndex]);
        }

        FreePool (BlessOverride);
        FreePool (Context);
        DEBUG ((DEBUG_ERROR, "OC: Failed to allocate bless overrides!\n"));
        return;
      }
    }

    Context->NumCustomBootPaths = Config->Misc.BlessOverride.Count;
    Context->CustomBootPaths    = BlessOverride;
  }

  Context->ScanPolicy           = Config->Misc.Security.ScanPolicy;
  Context->DmgLoading           = DmgLoading;
  Context->TimeoutSeconds       = Config->Misc.Boot.Timeout;
  Context->TakeoffDelay         = Config->Misc.Boot.TakeoffDelay;
  Context->StartImage           = StartImage;
  Context->CustomBootGuid       = CustomBootGuid;
  Context->LoaderHandle         = LoadHandle;
  Context->StorageContext       = Storage;
  Context->CustomRead           = OcToolLoadEntry;
  Context->PrivilegeContext     = Privilege;
  Context->RequestPrivilege     = OcShowSimplePasswordRequest;
  Context->VerifyPassword       = OcVerifyPassword;
  Context->ShowMenu             = OcShowSimpleBootMenu;
  Context->GetEntryLabelImage   = OcGetBootEntryLabelImage;
  Context->GetEntryIcon         = OcGetBootEntryIcon;
  Context->PlayAudioFile        = OcPlayAudioFile;
  Context->PlayAudioBeep        = OcPlayAudioBeep;
  Context->PlayAudioEntry       = OcPlayAudioEntry;
  Context->ToggleVoiceOver      = OcToggleVoiceOver;
  Context->PickerMode           = PickerMode;
  Context->ConsoleAttributes    = Config->Misc.Boot.ConsoleAttributes;
  Context->PickerAttributes     = Config->Misc.Boot.PickerAttributes;
  Context->PickerVariant        = AsciiPickerVariant;
  Context->BlacklistAppleUpdate = Config->Misc.Security.BlacklistAppleUpdate;

  if ((Config->Misc.Security.ExposeSensitiveData & OCS_EXPOSE_VERSION_UI) != 0) {
    Context->TitleSuffix      = OcMiscGetVersionString ();
    Context->WelcomeSuffix    = OcWelcomeString ();
  }

  if ((Config->Misc.Security.ExposeSensitiveData & OCS_EXPOSE_VERSION_UI) == 0) {
    Context->TempTitleSuffix  = OcMiscGetVersionString ();
     Context->TempWelcomSuffix = OcWelcomeString ();
  }
  Status = OcHandleRecoveryRequest (
             &Context->RecoveryInitiator
             );

  if (!EFI_ERROR (Status)) {
    Context->PickerCommand = OcPickerBootAppleRecovery;
  } else if (Config->Misc.Boot.ShowPicker && !Config->Misc.Boot.HibernateSkipsPicker) {
    Context->PickerCommand = OcPickerShowPicker;
  } else if (Config->Misc.Boot.ShowPicker && Config->Misc.Boot.HibernateSkipsPicker) {
    if (OcIsAppleHibernateWake ()) {
      Context->PickerCommand = OcPickerDefault;
    } else {
      Context->PickerCommand = OcPickerShowPicker;
    }
  } else {
    Context->PickerCommand = OcPickerDefault;
  }

  for (Index = 0, EntryIndex = 0; Index < Config->Misc.Entries.Count; ++Index) {
    if (Config->Misc.Entries.Values[Index]->Enabled) {
      Context->CustomEntries[EntryIndex].Name            = OC_BLOB_GET (&Config->Misc.Entries.Values[Index]->Name);
      Context->CustomEntries[EntryIndex].Path            = OC_BLOB_GET (&Config->Misc.Entries.Values[Index]->Path);
      Context->CustomEntries[EntryIndex].Arguments       = OC_BLOB_GET (&Config->Misc.Entries.Values[Index]->Arguments);
      Context->CustomEntries[EntryIndex].Flavour         = OC_BLOB_GET (&Config->Misc.Entries.Values[Index]->Flavour);
      Context->CustomEntries[EntryIndex].Auxiliary       = Config->Misc.Entries.Values[Index]->Auxiliary;
      Context->CustomEntries[EntryIndex].Tool            = FALSE;
      Context->CustomEntries[EntryIndex].TextMode        = Config->Misc.Entries.Values[Index]->TextMode;
      Context->CustomEntries[EntryIndex].RealPath        = TRUE;  ///< Always true for entries
      Context->CustomEntries[EntryIndex].FullNvramAccess = FALSE;
      ++EntryIndex;
    }
  }

  Context->AbsoluteEntryCount = EntryIndex;
  //
  // Due to the file size and sanity guarantees OcXmlLib makes,
  // EntryIndex cannot overflow.
  //
  for (Index = 0; Index < Config->Misc.Tools.Count; ++Index) {
    if (Config->Misc.Tools.Values[Index]->Enabled) {
      Context->CustomEntries[EntryIndex].Name            = OC_BLOB_GET (&Config->Misc.Tools.Values[Index]->Name);
      Context->CustomEntries[EntryIndex].Path            = OC_BLOB_GET (&Config->Misc.Tools.Values[Index]->Path);
      Context->CustomEntries[EntryIndex].Arguments       = OC_BLOB_GET (&Config->Misc.Tools.Values[Index]->Arguments);
      Context->CustomEntries[EntryIndex].Flavour         = OC_BLOB_GET (&Config->Misc.Tools.Values[Index]->Flavour);
      Context->CustomEntries[EntryIndex].Auxiliary       = Config->Misc.Tools.Values[Index]->Auxiliary;
      Context->CustomEntries[EntryIndex].Tool            = TRUE;
      Context->CustomEntries[EntryIndex].TextMode        = Config->Misc.Tools.Values[Index]->TextMode;
      Context->CustomEntries[EntryIndex].RealPath        = Config->Misc.Tools.Values[Index]->RealPath;
      Context->CustomEntries[EntryIndex].FullNvramAccess = Config->Misc.Tools.Values[Index]->FullNvramAccess;
      ++EntryIndex;
    }
  }

  Context->AllCustomEntryCount = EntryIndex;
  Context->PollAppleHotKeys    = Config->Misc.Boot.PollAppleHotKeys;
  Context->HideAuxiliary       = Config->Misc.Boot.HideAuxiliary;
  Context->PickerAudioAssist   = Config->Misc.Boot.PickerAudioAssist;

  OcPreLocateAudioProtocol (Context);

  DEBUG ((DEBUG_INFO, "OC: Ready for takeoff in %u us\n", (UINT32)Context->TakeoffDelay));

  OcLoadPickerHotKeys (Context);

  Context->AllowSetDefault = Config->Misc.Security.AllowSetDefault;

  if (Interface != NULL) {
    Status = Interface->PopulateContext (Interface, Storage, Context);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "OC: External interface failure, fallback to builtin - %r\n", Status));
    }
  }

  Status = OcRunBootPicker (Context);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OC: Failed to show boot menu!\n"));
  }
}

VOID
OcMiscUefiQuirksLoaded (
  IN OC_GLOBAL_CONFIG  *Config
  )
{
  if (  (  ((Config->Misc.Security.ScanPolicy & OC_SCAN_FILE_SYSTEM_LOCK) == 0)
        && ((Config->Misc.Security.ScanPolicy & OC_SCAN_FILE_SYSTEM_BITS) != 0))
     || (  ((Config->Misc.Security.ScanPolicy & OC_SCAN_DEVICE_LOCK) == 0)
        && ((Config->Misc.Security.ScanPolicy & OC_SCAN_DEVICE_BITS) != 0)))
  {
    DEBUG ((DEBUG_ERROR, "OC: Invalid ScanPolicy %X\n", Config->Misc.Security.ScanPolicy));
    CpuDeadLoop ();
  }

  //
  // Inform drivers about our scan policy.
  //
  OcSetSystemVariable (
    OC_SCAN_POLICY_VARIABLE_NAME,
    OPEN_CORE_INT_NVRAM_ATTR,
    sizeof (Config->Misc.Security.ScanPolicy),
    &Config->Misc.Security.ScanPolicy,
    NULL
    );
}
