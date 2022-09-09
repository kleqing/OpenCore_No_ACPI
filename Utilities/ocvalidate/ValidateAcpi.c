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

/**
  Callback function to verify whether Path is duplicated in ACPI->Add.

  @param[in]  PrimaryEntry    Primary entry to be checked.
  @param[in]  SecondaryEntry  Secondary entry to be checked.
  @retval     TRUE            If PrimaryEntry and SecondaryEntry are duplicated.
**/
STATIC
BOOLEAN
ACPIAddHasDuplication (
  IN  CONST VOID  *PrimaryEntry,
  IN  CONST VOID  *SecondaryEntry
  )
{
  CONST OC_ACPI_ADD_ENTRY  *ACPIAddPrimaryEntry;
  CONST OC_ACPI_ADD_ENTRY  *ACPIAddSecondaryEntry;
  CONST CHAR8              *ACPIAddPrimaryPathString;
  CONST CHAR8              *ACPIAddSecondaryPathString;

  ACPIAddPrimaryEntry        = *(CONST OC_ACPI_ADD_ENTRY **)PrimaryEntry;
  ACPIAddSecondaryEntry      = *(CONST OC_ACPI_ADD_ENTRY **)SecondaryEntry;
  ACPIAddPrimaryPathString   = OC_BLOB_GET (&ACPIAddPrimaryEntry->Path);
  ACPIAddSecondaryPathString = OC_BLOB_GET (&ACPIAddSecondaryEntry->Path);

  if (!ACPIAddPrimaryEntry->Enabled || !ACPIAddSecondaryEntry->Enabled) {
    return FALSE;
  }

  return StringIsDuplicated ("ACPI->Add", ACPIAddPrimaryPathString, ACPIAddSecondaryPathString);
}

STATIC
UINT32
CheckACPIAdd (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       Index;
  CONST CHAR8  *Path;
  CONST CHAR8  *Comment;
  UINTN        AcpiAddSumSize;

  ErrorCount = 0;

  for (Index = 0; Index < Config->Acpi.Add.Count; ++Index) {
    Path    = OC_BLOB_GET (&Config->Acpi.Add.Values[Index]->Path);
    Comment = OC_BLOB_GET (&Config->Acpi.Add.Values[Index]->Comment);

    //
    // Sanitise strings.
    //
    if (!AsciiFileSystemPathIsLegal (Path)) {
      DEBUG ((DEBUG_WARN, "ACPI->Add[%u]->路径包含非法字符,建议不要使用中文字符!\n", Index));
      ++ErrorCount;
      continue;
    }

    if (!AsciiCommentIsLegal (Comment)) {
      DEBUG ((DEBUG_WARN, "ACPI->Add[%u]->Comment中包含非法字符,建议不要使用中文字符!\n", Index));
      ++ErrorCount;
    }

    if (!OcAsciiEndsWith (Path, ".aml", TRUE) && !OcAsciiEndsWith (Path, ".bin", TRUE)) {
      DEBUG ((DEBUG_WARN, "ACPI->Add[%u]->路径具有.aml和.bin以外的文件名后缀!\n", Index));
      ++ErrorCount;
    }

    //
    // Check the length of path relative to OC directory.
    //
    AcpiAddSumSize = L_STR_LEN (OPEN_CORE_ACPI_PATH) + AsciiStrSize (Path);
    if (AcpiAddSumSize > OC_STORAGE_SAFE_PATH_MAX) {
      DEBUG ((
        DEBUG_WARN,
        "ACPI->Add[%u]->Path (%u长度) 太长 (不应超过 %u)!\n",
        Index,
        AsciiStrLen (Path),
        OC_STORAGE_SAFE_PATH_MAX - L_STR_LEN (OPEN_CORE_ACPI_PATH)
        ));
      ++ErrorCount;
    }
  }

  //
  // Check duplicated entries in ACPI->Add.
  //
  ErrorCount += FindArrayDuplication (
                  Config->Acpi.Add.Values,
                  Config->Acpi.Add.Count,
                  sizeof (Config->Acpi.Add.Values[0]),
                  ACPIAddHasDuplication
                  );

  return ErrorCount;
}

STATIC
UINT32
CheckACPIDelete (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       Index;
  CONST CHAR8  *Comment;

  ErrorCount = 0;

  for (Index = 0; Index < Config->Acpi.Delete.Count; ++Index) {
    Comment = OC_BLOB_GET (&Config->Acpi.Delete.Values[Index]->Comment);

    //
    // Sanitise strings.
    //
    if (!AsciiCommentIsLegal (Comment)) {
      DEBUG ((DEBUG_WARN, "ACPI->Delete[%u]->Comment中包含非法字符,建议不要使用中文字符!\n", Index));
      ++ErrorCount;
    }

    //
    // Size of OemTableId and TableSignature cannot be checked,
    // as serialisation kills it.
    //
  }

  return ErrorCount;
}

STATIC
UINT32
CheckACPIPatch (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       Index;
  CONST CHAR8  *Comment;
  CONST UINT8  *Find;
  UINT32       FindSize;
  CONST UINT8  *Replace;
  UINT32       ReplaceSize;
  CONST UINT8  *Mask;
  UINT32       MaskSize;
  CONST UINT8  *ReplaceMask;
  UINT32       ReplaceMaskSize;

  ErrorCount = 0;

  for (Index = 0; Index < Config->Acpi.Patch.Count; ++Index) {
    Comment         = OC_BLOB_GET (&Config->Acpi.Patch.Values[Index]->Comment);
    Find            = OC_BLOB_GET (&Config->Acpi.Patch.Values[Index]->Find);
    FindSize        = Config->Acpi.Patch.Values[Index]->Find.Size;
    Replace         = OC_BLOB_GET (&Config->Acpi.Patch.Values[Index]->Replace);
    ReplaceSize     = Config->Acpi.Patch.Values[Index]->Replace.Size;
    Mask            = OC_BLOB_GET (&Config->Acpi.Patch.Values[Index]->Mask);
    MaskSize        = Config->Acpi.Patch.Values[Index]->Mask.Size;
    ReplaceMask     = OC_BLOB_GET (&Config->Acpi.Patch.Values[Index]->ReplaceMask);
    ReplaceMaskSize = Config->Acpi.Patch.Values[Index]->ReplaceMask.Size;

    //
    // Sanitise strings.
    //
    if (!AsciiCommentIsLegal (Comment)) {
      DEBUG ((DEBUG_WARN, "ACPI->Patch[%u]->Comment中包含非法字符,建议不要使用中文字符!\n", Index));
      ++ErrorCount;
    }

    //
    // Size of OemTableId and TableSignature cannot be checked,
    // as serialisation kills it.
    //

    //
    // Checks for size.
    //
    ErrorCount += ValidatePatch (
                    "ACPI->Patch",
                    Index,
                    FALSE,
                    Find,
                    FindSize,
                    Replace,
                    ReplaceSize,
                    Mask,
                    MaskSize,
                    ReplaceMask,
                    ReplaceMaskSize
                    );
  }

  return ErrorCount;
}

UINT32
CheckACPI (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32               ErrorCount;
  UINTN                Index;
  STATIC CONFIG_CHECK  ACPICheckers[] = {
    &CheckACPIAdd,
    &CheckACPIDelete,
    &CheckACPIPatch
  };

  DEBUG ((DEBUG_VERBOSE, "配置加载到 %a!\n", __func__));

  ErrorCount = 0;

  for (Index = 0; Index < ARRAY_SIZE (ACPICheckers); ++Index) {
    ErrorCount += ACPICheckers[Index](Config);
  }

  return ReportError (__func__, ErrorCount);
}
