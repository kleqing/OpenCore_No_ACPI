/** @file
  Copyright (c) 2020, PMheart. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause
**/

#ifndef OC_USER_GLOBAL_VAR_H
#define OC_USER_GLOBAL_VAR_H

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <stdlib.h>

#ifdef SANITIZE_TEST
  #include <sanitizer/asan_interface.h>
#define ASAN_CHECK_MEMORY_REGION(addr, size) \
  do { if (__asan_region_is_poisoned((addr), (size)) != NULL) { abort(); } } while (0)
#else
#define ASAN_POISON_MEMORY_REGION(addr, size)    do { } while (0)
#define ASAN_UNPOISON_MEMORY_REGION(addr, size)  do { } while (0)
#define ASAN_CHECK_MEMORY_REGION(addr, size)     do { } while (0)
#endif

extern EFI_GUID  gAppleBootVariableGuid;
extern EFI_GUID  gAppleEventProtocolGuid;
extern EFI_GUID  gAppleKeyMapAggregatorProtocolGuid;
extern EFI_GUID  gAppleKeyMapDatabaseProtocolGuid;
extern EFI_GUID  gAppleApfsContainerInfoGuid;
extern EFI_GUID  gAppleApfsVolumeInfoGuid;
extern EFI_GUID  gAppleBlessedOsxFolderInfoGuid;
extern EFI_GUID  gAppleBlessedSystemFileInfoGuid;
extern EFI_GUID  gAppleBlessedSystemFolderInfoGuid;
extern EFI_GUID  gAppleBootPolicyProtocolGuid;
extern EFI_GUID  gAppleVendorVariableGuid;
extern EFI_GUID  gAppleImg4VerificationProtocolGuid;
extern EFI_GUID  gAppleBeepGenProtocolGuid;
extern EFI_GUID  gApplePlatformInfoDatabaseProtocolGuid;
extern EFI_GUID  gAppleFsbFrequencyPlatformInfoGuid;
extern EFI_GUID  gAppleFsbFrequencyPlatformInfoIndexHobGuid;

extern CONST CHAR8  *gEfiCallerBaseName;
extern EFI_GUID     gEfiGraphicsOutputProtocolGuid;
extern EFI_GUID     gEfiHiiFontProtocolGuid;
extern EFI_GUID     gEfiSimpleTextOutProtocolGuid;
extern EFI_GUID     gEfiUgaDrawProtocolGuid;
extern EFI_GUID     gEfiAbsolutePointerProtocolGuid;
extern EFI_GUID     gEfiLoadedImageProtocolGuid;
extern EFI_GUID     gEfiShellParametersProtocolGuid;
extern EFI_GUID     gEfiSimplePointerProtocolGuid;
extern EFI_GUID     gEfiDebugPortProtocolGuid;
extern EFI_GUID     gEfiDevicePathProtocolGuid;
extern EFI_GUID     gEfiPcAnsiGuid;
extern EFI_GUID     gEfiPersistentVirtualCdGuid;
extern EFI_GUID     gEfiPersistentVirtualDiskGuid;
extern EFI_GUID     gEfiSasDevicePathGuid;
extern EFI_GUID     gEfiUartDevicePathGuid;
extern EFI_GUID     gEfiVT100Guid;
extern EFI_GUID     gEfiVT100PlusGuid;
extern EFI_GUID     gEfiVTUTF8Guid;
extern EFI_GUID     gEfiVirtualCdGuid;
extern EFI_GUID     gEfiVirtualDiskGuid;
extern EFI_GUID     gEfiFileInfoGuid;
extern EFI_GUID     gEfiFileSystemVolumeLabelInfoIdGuid;
extern EFI_GUID     gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID     gEfiUserInterfaceThemeProtocolGuid;
extern EFI_GUID     gEfiMpServiceProtocolGuid;
extern EFI_GUID     gFrameworkEfiMpServiceProtocolGuid;
extern EFI_GUID     gEfiGlobalVariableGuid;
extern EFI_GUID     gEfiSmbios3TableGuid;
extern EFI_GUID     gEfiLegacyRegionProtocolGuid;
extern EFI_GUID     gEfiLegacyRegion2ProtocolGuid;
extern EFI_GUID     gEfiPciRootBridgeIoProtocolGuid;
extern EFI_GUID     gEfiSmbiosTableGuid;
extern EFI_GUID     gEfiUnicodeCollation2ProtocolGuid;
extern EFI_GUID     gEfiFileSystemInfoGuid;
extern EFI_GUID     gEfiDiskIoProtocolGuid;
extern EFI_GUID     gEfiBlockIoProtocolGuid;
extern EFI_GUID     gEfiDriverBindingProtocolGuid;
extern EFI_GUID     gEfiComponentNameProtocolGuid;

extern EFI_GUID  gOcBootstrapProtocolGuid;
extern EFI_GUID  gOcVendorVariableGuid;
extern EFI_GUID  gOcCustomSmbios3TableGuid;
extern EFI_GUID  gOcCustomSmbiosTableGuid;
extern EFI_GUID  gOcAudioProtocolGuid;

#endif // OC_USER_GLOBAL_VAR_H
