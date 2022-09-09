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

#include <Library/OcMainLib.h>

#include <UserFile.h>

UINT32
CheckConfig (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32               ErrorCount;
  UINT32               CurrErrorCount;
  UINTN                Index;
  STATIC CONFIG_CHECK  ConfigCheckers[] = {
    &CheckACPI,
    &CheckBooter,
    &CheckDeviceProperties,
    &CheckKernel,
    &CheckMisc,
    &CheckNvram,
    &CheckPlatformInfo,
    &CheckUefi
  };

  ErrorCount     = 0;
  CurrErrorCount = 0;

  //
  // Pass config structure to all checkers.
  //
  for (Index = 0; Index < ARRAY_SIZE (ConfigCheckers); ++Index) {
    CurrErrorCount = ConfigCheckers[Index](Config);

    if (CurrErrorCount != 0) {
      //
      // Print an extra newline on error.
      //
      DEBUG ((DEBUG_WARN, "\n"));
      ErrorCount += CurrErrorCount;
    }
  }

  return ErrorCount;
}

int
ENTRY_POINT (
  int   argc,
  char  *argv[]
  )
{
  UINT8             *ConfigFileBuffer;
  UINT32            ConfigFileSize;
  CONST CHAR8       *ConfigFileName;
  INT64             ExecTimeStart;
  OC_GLOBAL_CONFIG  Config;
  EFI_STATUS        Status;
  UINT32            ErrorCount;

  ErrorCount = 0;

  //
  // Enable PCD debug logging.
  //
  PcdGet8 (PcdDebugPropertyMask)          |= DEBUG_PROPERTY_DEBUG_CODE_ENABLED;
  PcdGet32 (PcdFixedDebugPrintErrorLevel) |= DEBUG_INFO;
  PcdGet32 (PcdDebugPrintErrorLevel)      |= DEBUG_INFO;

  DEBUG ((DEBUG_ERROR, "\n注意: 此版本的 ocvalidate 仅兼容 OpenCore 版本 %a!\n\n", OPEN_CORE_VERSION));

  //
  // Print usage.
  //
  if (argc != 2) {
    DEBUG ((DEBUG_ERROR, "用法: %a <指定路径/config.plist>\n\n", argv[0]));
    return -1;
  }

  //
  // Read config file (Only one single config is supported).
  //
  ConfigFileName   = argv[1];
  ConfigFileBuffer = UserReadFile (ConfigFileName, &ConfigFileSize);
  if (ConfigFileBuffer == NULL) {
    DEBUG ((DEBUG_ERROR, "读取 %a 失败\n", ConfigFileName));
    return -1;
  }

  //
  // Record the current time when action starts.
  //
  ExecTimeStart = GetCurrentTimestamp ();

  //
  // Initialise config structure to be checked, and exit on error.
  //
  Status = OcConfigurationInit (&Config, ConfigFileBuffer, ConfigFileSize, &ErrorCount);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "无效的配置\n"));
    return -1;
  }

  if (ErrorCount > 0) {
    DEBUG ((DEBUG_ERROR, "配置检查发现%u个%a!\n", ErrorCount, ErrorCount > 1 ? "错误" : "错误"));
  }

  //
  // Print a newline that splits errors between OcConfigurationInit and config checkers.
  //
  DEBUG ((DEBUG_ERROR, "\n"));
  ErrorCount += CheckConfig (&Config);

  OcConfigurationFree (&Config);
  FreePool (ConfigFileBuffer);

  if (ErrorCount == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "验证%a用了%llu毫秒完成,未发现问题.\n",
      ConfigFileName,
      GetCurrentTimestamp () - ExecTimeStart
      ));
  } else {
    DEBUG ((
      DEBUG_ERROR,
      "验证%a用了%llu毫秒完成，发现%u个%a需要注意.\n",
      ConfigFileName,
      GetCurrentTimestamp () - ExecTimeStart,
      ErrorCount,
      ErrorCount > 1 ? "问题" : "问题"
      ));

    return EXIT_FAILURE;
  }

  return 0;
}

int
LLVMFuzzerTestOneInput (
  const uint8_t  *Data,
  size_t         Size
  )
{
  VOID              *NewData;
  OC_GLOBAL_CONFIG  Config;

  NewData = AllocatePool (Size);
  if (NewData != NULL) {
    CopyMem (NewData, Data, Size);
    OcConfigurationInit (&Config, NewData, Size, NULL);
    OcConfigurationFree (&Config);
    FreePool (NewData);
  }

  return 0;
}
