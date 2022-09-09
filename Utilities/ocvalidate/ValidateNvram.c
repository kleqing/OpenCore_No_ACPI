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
#include "NvramKeyInfo.h"

/**
  Callback function to verify whether one entry is duplicated in NVRAM->Add.

  @param[in]  PrimaryEntry    Primary entry to be checked.
  @param[in]  SecondaryEntry  Secondary entry to be checked.

  @retval     TRUE            If PrimaryEntry and SecondaryEntry are duplicated.
**/
STATIC
BOOLEAN
NvramAddHasDuplication (
  IN  CONST VOID  *PrimaryEntry,
  IN  CONST VOID  *SecondaryEntry
  )
{
  CONST OC_STRING  *NvramAddPrimaryEntry;
  CONST OC_STRING  *NvramAddSecondaryEntry;
  CONST CHAR8      *NvramAddPrimaryGUIDString;
  CONST CHAR8      *NvramAddSecondaryGUIDString;

  NvramAddPrimaryEntry        = *(CONST OC_STRING **)PrimaryEntry;
  NvramAddSecondaryEntry      = *(CONST OC_STRING **)SecondaryEntry;
  NvramAddPrimaryGUIDString   = OC_BLOB_GET (NvramAddPrimaryEntry);
  NvramAddSecondaryGUIDString = OC_BLOB_GET (NvramAddSecondaryEntry);

  return StringIsDuplicated ("NVRAM->Add", NvramAddPrimaryGUIDString, NvramAddSecondaryGUIDString);
}

/**
  Callback function to verify whether one entry is duplicated in NVRAM->Delete.

  @param[in]  PrimaryEntry    Primary entry to be checked.
  @param[in]  SecondaryEntry  Secondary entry to be checked.

  @retval     TRUE            If PrimaryEntry and SecondaryEntry are duplicated.
**/
STATIC
BOOLEAN
NvramDeleteHasDuplication (
  IN  CONST VOID  *PrimaryEntry,
  IN  CONST VOID  *SecondaryEntry
  )
{
  CONST OC_STRING  *NvramDeletePrimaryEntry;
  CONST OC_STRING  *NvramDeleteSecondaryEntry;
  CONST CHAR8      *NvramDeletePrimaryGUIDString;
  CONST CHAR8      *NvramDeleteSecondaryGUIDString;

  NvramDeletePrimaryEntry        = *(CONST OC_STRING **)PrimaryEntry;
  NvramDeleteSecondaryEntry      = *(CONST OC_STRING **)SecondaryEntry;
  NvramDeletePrimaryGUIDString   = OC_BLOB_GET (NvramDeletePrimaryEntry);
  NvramDeleteSecondaryGUIDString = OC_BLOB_GET (NvramDeleteSecondaryEntry);

  return StringIsDuplicated ("NVRAM->Delete", NvramDeletePrimaryGUIDString, NvramDeleteSecondaryGUIDString);
}

/**
  Callback function to verify whether one entry is duplicated in NVRAM->LegacySchema.

  @param[in]  PrimaryEntry    Primary entry to be checked.
  @param[in]  SecondaryEntry  Secondary entry to be checked.

  @retval     TRUE            If PrimaryEntry and SecondaryEntry are duplicated.
**/
STATIC
BOOLEAN
NvramLegacySchemaHasDuplication (
  IN  CONST VOID  *PrimaryEntry,
  IN  CONST VOID  *SecondaryEntry
  )
{
  CONST OC_STRING  *NvramLegacySchemaPrimaryEntry;
  CONST OC_STRING  *NvramLegacySchemaSecondaryEntry;
  CONST CHAR8      *NvramLegacySchemaPrimaryGUIDString;
  CONST CHAR8      *NvramLegacySchemaSecondaryGUIDString;

  NvramLegacySchemaPrimaryEntry        = *(CONST OC_STRING **)PrimaryEntry;
  NvramLegacySchemaSecondaryEntry      = *(CONST OC_STRING **)SecondaryEntry;
  NvramLegacySchemaPrimaryGUIDString   = OC_BLOB_GET (NvramLegacySchemaPrimaryEntry);
  NvramLegacySchemaSecondaryGUIDString = OC_BLOB_GET (NvramLegacySchemaSecondaryEntry);

  return StringIsDuplicated ("NVRAM->LegacySchema", NvramLegacySchemaPrimaryGUIDString, NvramLegacySchemaSecondaryGUIDString);
}

STATIC
UINT32
ValidateNvramKeyByGuid (
  IN  CONST CHAR8     *AsciiGuid,
  IN  CONST OC_ASSOC  *VariableMap
  )
{
  UINT32      ErrorCount;
  EFI_STATUS  Status;
  GUID        Guid;
  UINT32      VariableIndex;
  UINTN       Index;
  UINTN       Index2;

  ErrorCount = 0;

  Status = AsciiStrToGuid (AsciiGuid, &Guid);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NVRAM->Add: 由于GUID格式错误，无法检查%a!\n", AsciiGuid));
    ++ErrorCount;
    return ErrorCount;
  }

  for (Index = 0; Index < mGUIDMapsCount; ++Index) {
    if (CompareGuid (&Guid, mGUIDMaps[Index].Guid)) {
      for (VariableIndex = 0; VariableIndex < VariableMap->Count; ++VariableIndex) {
        for (Index2 = 0; Index2 < mGUIDMaps[Index].NvramKeyMapsCount; ++Index2) {
          if (AsciiStrCmp (mGUIDMaps[Index].NvramKeyMaps[Index2].KeyName, OC_BLOB_GET (VariableMap->Keys[VariableIndex])) == 0) {
            if (!mGUIDMaps[Index].NvramKeyMaps[Index2].KeyChecker (
                                                         OC_BLOB_GET (VariableMap->Values[VariableIndex]),
                                                         VariableMap->Values[VariableIndex]->Size
                                                         ))
            {
              DEBUG ((
                DEBUG_WARN,
                "NVRAM->Add->%g->%a 为无效值!\n",
                &Guid,
                OC_BLOB_GET (VariableMap->Keys[VariableIndex])
                ));
              ++ErrorCount;
            } else {
              //
              // Special check for UIScale when it is valid.
              //
              if (AsciiStrCmp (OC_BLOB_GET (VariableMap->Keys[VariableIndex]), "UIScale") == 0) {
                mHasNvramUIScale = TRUE;
              }
            }
          }
        }
      }
    }
  }

  return ErrorCount;
}

STATIC
UINT32
CheckNvramAdd (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       GuidIndex;
  UINT32       VariableIndex;
  CONST CHAR8  *AsciiGuid;
  CONST CHAR8  *AsciiNvramKey;
  OC_ASSOC     *VariableMap;

  ErrorCount = 0;

  for (GuidIndex = 0; GuidIndex < Config->Nvram.Add.Count; ++GuidIndex) {
    AsciiGuid = OC_BLOB_GET (Config->Nvram.Add.Keys[GuidIndex]);

    if (!AsciiGuidIsLegal (AsciiGuid)) {
      DEBUG ((DEBUG_WARN, "NVRAM->Add[%u] 有一个错误的GUID!\n", GuidIndex));
      ++ErrorCount;
    }

    VariableMap = Config->Nvram.Add.Values[GuidIndex];

    for (VariableIndex = 0; VariableIndex < VariableMap->Count; ++VariableIndex) {
      AsciiNvramKey = OC_BLOB_GET (VariableMap->Keys[VariableIndex]);

      //
      // Sanitise strings.
      //
      if (!AsciiPropertyIsLegal (AsciiNvramKey)) {
        DEBUG ((
          DEBUG_WARN,
          "NVRAM->Add[%u]->Key[%u] 包含非法字符!\n",
          GuidIndex,
          VariableIndex
          ));
        ++ErrorCount;
      }
    }

    //
    // Check duplicated properties in NVRAM->Add.
    //
    ErrorCount += FindArrayDuplication (
                    VariableMap->Keys,
                    VariableMap->Count,
                    sizeof (VariableMap->Keys[0]),
                    NvramAddHasDuplication
                    );

    //
    // Check for accepted values for NVRAM keys.
    //
    ErrorCount += ValidateNvramKeyByGuid (AsciiGuid, VariableMap);
  }

  //
  // Check duplicated entries in NVRAM->Add.
  //
  ErrorCount += FindArrayDuplication (
                  Config->Nvram.Add.Keys,
                  Config->Nvram.Add.Count,
                  sizeof (Config->Nvram.Add.Keys[0]),
                  NvramAddHasDuplication
                  );

  return ErrorCount;
}

STATIC
UINT32
CheckNvramDelete (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       GuidIndex;
  UINT32       VariableIndex;
  CONST CHAR8  *AsciiGuid;
  CONST CHAR8  *AsciiNvramKey;

  ErrorCount = 0;

  for (GuidIndex = 0; GuidIndex < Config->Nvram.Delete.Count; ++GuidIndex) {
    AsciiGuid = OC_BLOB_GET (Config->Nvram.Delete.Keys[GuidIndex]);

    if (!AsciiGuidIsLegal (AsciiGuid)) {
      DEBUG ((DEBUG_WARN, "NVRAM->Delete[%u] 是一个错误的GUID!\n", GuidIndex));
      ++ErrorCount;
    }

    for (VariableIndex = 0; VariableIndex < Config->Nvram.Delete.Values[GuidIndex]->Count; ++VariableIndex) {
      AsciiNvramKey = OC_BLOB_GET (Config->Nvram.Delete.Values[GuidIndex]->Values[VariableIndex]);

      //
      // Sanitise strings.
      //
      if (!AsciiPropertyIsLegal (AsciiNvramKey)) {
        DEBUG ((
          DEBUG_WARN,
          "NVRAM->Delete[%u]->Key[%u] 包含非法字符!\n",
          GuidIndex,
          VariableIndex
          ));
        ++ErrorCount;
      }
    }

    //
    // Check duplicated properties in NVRAM->Delete.
    //
    ErrorCount += FindArrayDuplication (
                    Config->Nvram.Delete.Values[GuidIndex]->Values,
                    Config->Nvram.Delete.Values[GuidIndex]->Count,
                    sizeof (Config->Nvram.Delete.Values[GuidIndex]->Values[0]),
                    NvramDeleteHasDuplication
                    );
  }

  //
  // Check duplicated entries in NVRAM->Delete.
  //
  ErrorCount += FindArrayDuplication (
                  Config->Nvram.Delete.Keys,
                  Config->Nvram.Delete.Count,
                  sizeof (Config->Nvram.Delete.Keys[0]),
                  NvramDeleteHasDuplication
                  );

  return ErrorCount;
}

STATIC
UINT32
CheckNvramSchema (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32       ErrorCount;
  UINT32       GuidIndex;
  UINT32       VariableIndex;
  CONST CHAR8  *AsciiGuid;
  CONST CHAR8  *AsciiNvramKey;

  ErrorCount = 0;

  for (GuidIndex = 0; GuidIndex < Config->Nvram.Legacy.Count; ++GuidIndex) {
    AsciiGuid = OC_BLOB_GET (Config->Nvram.Legacy.Keys[GuidIndex]);

    if (!AsciiGuidIsLegal (AsciiGuid)) {
      DEBUG ((DEBUG_WARN, "NVRAM->LegacySchema[%u] 是一个错误的GUID!\n", GuidIndex));
      ++ErrorCount;
    }

    for (VariableIndex = 0; VariableIndex < Config->Nvram.Legacy.Values[GuidIndex]->Count; ++VariableIndex) {
      AsciiNvramKey = OC_BLOB_GET (Config->Nvram.Legacy.Values[GuidIndex]->Values[VariableIndex]);

      //
      // Sanitise strings.
      //
      if (!AsciiPropertyIsLegal (AsciiNvramKey)) {
        DEBUG ((
          DEBUG_WARN,
          "NVRAM->LegacySchema[%u]->Key[%u] 包含非法字符!\n",
          GuidIndex,
          VariableIndex
          ));
        ++ErrorCount;
      }
    }

    //
    // Check duplicated properties in NVRAM->LegacySchema.
    //
    ErrorCount += FindArrayDuplication (
                    Config->Nvram.Legacy.Values[GuidIndex]->Values,
                    Config->Nvram.Legacy.Values[GuidIndex]->Count,
                    sizeof (Config->Nvram.Legacy.Values[GuidIndex]->Values[0]),
                    NvramLegacySchemaHasDuplication
                    );
  }

  //
  // Check duplicated entries in NVRAM->LegacySchema.
  //
  ErrorCount += FindArrayDuplication (
                  Config->Nvram.Legacy.Keys,
                  Config->Nvram.Legacy.Count,
                  sizeof (Config->Nvram.Legacy.Keys[0]),
                  NvramLegacySchemaHasDuplication
                  );

  return ErrorCount;
}

UINT32
CheckNvram (
  IN  OC_GLOBAL_CONFIG  *Config
  )
{
  UINT32               ErrorCount;
  UINTN                Index;
  STATIC CONFIG_CHECK  NvramCheckers[] = {
    &CheckNvramAdd,
    &CheckNvramDelete,
    &CheckNvramSchema
  };

  DEBUG ((DEBUG_VERBOSE, "config loaded into %a!\n", __func__));

  ErrorCount = 0;

  for (Index = 0; Index < ARRAY_SIZE (NvramCheckers); ++Index) {
    ErrorCount += NvramCheckers[Index](Config);
  }

  return ReportError (__func__, ErrorCount);
}
