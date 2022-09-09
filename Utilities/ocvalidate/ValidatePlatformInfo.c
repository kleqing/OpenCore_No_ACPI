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

#include "ocvalidate.h"
#include "OcValidateLib.h"

#include <Library/OcMacInfoLib.h>
#include <IndustryStandard/AppleSmBios.h>

STATIC
BOOLEAN
ValidateProcessorType (
  IN  UINT16  ProcessorType
  )
{
  UINTN               Index;
  STATIC CONST UINT8  AllowedProcessorType[] = {
    AppleProcessorMajorCore,
    AppleProcessorMajorCore2,
    AppleProcessorMajorXeonPenryn,
    AppleProcessorMajorXeonNehalem,
    AppleProcessorMajorI5,
    AppleProcessorMajorI7,
    AppleProcessorMajorI3,
    AppleProcessorMajorI9,
    AppleProcessorMajorXeonE5,
    AppleProcessorMajorM,
    AppleProcessorMajorM3,
    AppleProcessorMajorM5,
    AppleProcessorMajorM7,
    AppleProcessorMajorXeonW
  };

  //
  // 0 is allowed.
  //
  if (ProcessorType == 0U) {
    return TRUE;
  }

  for (Index = 0; Index < ARRAY_SIZE (AllowedProcessorType); ++Index) {
    if ((ProcessorType >> 8U) == AllowedProcessorType[Index]) {
      return TRUE;
    }
  }

  return FALSE;
}

//
// NOTE: Only PlatformInfo->Generic is checked here. The rest is ignored.
//

STATIC
UINT32
CheckPlatformInfoGeneric (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  CONST CHAR8  *SystemProductName;
  CONST CHAR8  *SystemMemoryStatus;
  CONST CHAR8  *AsciiSystemUUID;
  UINT16       ProcessorType;

  ErrorCount = 0;

  SystemProductName = OC_BLOB_GET (&Config->PlatformInfo.Generic.SystemProductName);
  if (!HasMacInfo (SystemProductName)) {
    DEBUG ((DEBUG_WARN, "PlatformInfo->Generic->SystemProductName 设置了未知的Model!\n"));
    ++ErrorCount;
  }

  SystemMemoryStatus = OC_BLOB_GET (&Config->PlatformInfo.Generic.SystemMemoryStatus);
  if (  (AsciiStrCmp (SystemMemoryStatus, "Auto") != 0)
     && (AsciiStrCmp (SystemMemoryStatus, "Upgradable") != 0)
     && (AsciiStrCmp (SystemMemoryStatus, "Soldered") != 0))
  {
    DEBUG ((DEBUG_WARN, "PlatformInfo->Generic->SystemMemoryStatus 不太对(只能是 Auto, Upgradable, 或 Soldered)!\n"));
    ++ErrorCount;
  }

  AsciiSystemUUID = OC_BLOB_GET (&Config->PlatformInfo.Generic.SystemUuid);
  if (  (AsciiSystemUUID[0] != '\0')
     && (AsciiStrCmp (AsciiSystemUUID, "OEM") != 0)
     && !AsciiGuidIsLegal (AsciiSystemUUID))
  {
    DEBUG ((DEBUG_WARN, "PlatformInfo->Generic->SystemUUID不正确 (只能为空、指定OEM字符串或有效的UUID)!\n"));
    ++ErrorCount;
  }

  ProcessorType = Config->PlatformInfo.Generic.ProcessorType;
  if (!ValidateProcessorType (ProcessorType)) {
    DEBUG ((DEBUG_WARN, "PlatformInfo->Generic->ProcessorType 是错误的!\n"));
    ++ErrorCount;
  }

  //
  // TODO: Sanitise MLB, ProcessorType, and SystemSerialNumber if possible...
  //

  return ErrorCount;
}

UINT32
CheckPlatformInfo (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32               ErrorCount;
  BOOLEAN              IsAutomaticEnabled;
  CONST CHAR8          *UpdateSMBIOSMode;
  UINTN                Index;
  STATIC CONFIG_CHECK  PlatformInfoCheckers[] = {
    &CheckPlatformInfoGeneric
  };

  DEBUG ((DEBUG_VERBOSE, "config loaded into %a!\n", __func__));

  ErrorCount = 0;

  UpdateSMBIOSMode = OC_BLOB_GET (&Config->PlatformInfo.UpdateSmbiosMode);
  if (  (AsciiStrCmp (UpdateSMBIOSMode, "TryOverwrite") != 0)
     && (AsciiStrCmp (UpdateSMBIOSMode, "Create") != 0)
     && (AsciiStrCmp (UpdateSMBIOSMode, "Overwrite") != 0)
     && (AsciiStrCmp (UpdateSMBIOSMode, "Custom") != 0))
  {
    DEBUG ((DEBUG_WARN, "PlatformInfo->UpdateSMBIOSMode 是错误的 (只能是TryOverwrite, Create, Overwrite, 或 Custom)!\n"));
    ++ErrorCount;
  }

  IsAutomaticEnabled = Config->PlatformInfo.Automatic;
  if (!IsAutomaticEnabled) {
    //
    // This is not an error, but we need to stop checking further.
    //
    return ReportError (__func__, ErrorCount);
  }

  for (Index = 0; Index < ARRAY_SIZE (PlatformInfoCheckers); ++Index) {
    ErrorCount += PlatformInfoCheckers[Index](Config);
  }

  return ReportError (__func__, ErrorCount);
}
