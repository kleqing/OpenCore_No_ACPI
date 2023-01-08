/** @file
  Copyright (c) 2020, PMheart. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <UserPcd.h>

#define _PCD_VALUE_PcdUefiLibMaxPrintBufferSize         320U
#define _PCD_VALUE_PcdUgaConsumeSupport                 ((BOOLEAN)1U)
#define _PCD_VALUE_PcdDebugPropertyMask                 0x23U
#define _PCD_VALUE_PcdDebugClearMemoryValue             0xAFU
#define _PCD_VALUE_PcdFixedDebugPrintErrorLevel         0x80000002U
#define _PCD_VALUE_PcdDebugPrintErrorLevel              0x80000002U
#define _PCD_VALUE_PcdMaximumAsciiStringLength          0U
#define _PCD_VALUE_PcdMaximumUnicodeStringLength        1000000U
#define _PCD_VALUE_PcdMaximumLinkedListLength           1000000U
#define _PCD_VALUE_PcdVerifyNodeInList                  ((BOOLEAN)0U)
#define _PCD_VALUE_PcdCpuNumberOfReservedVariableMtrrs  0x2U
#define _PCD_VALUE_PcdMaximumDevicePathNodeCount        0U

UINT32   _gPcd_FixedAtBuild_PcdUefiLibMaxPrintBufferSize             = _PCD_VALUE_PcdUefiLibMaxPrintBufferSize;
BOOLEAN  _gPcd_FixedAtBuild_PcdUgaConsumeSupport                     = _PCD_VALUE_PcdUgaConsumeSupport;
UINT8    _gPcd_FixedAtBuild_PcdDebugPropertyMask                     = _PCD_VALUE_PcdDebugPropertyMask;
UINT8    _gPcd_FixedAtBuild_PcdDebugClearMemoryValue                 = _PCD_VALUE_PcdDebugClearMemoryValue;
UINT32   _gPcd_FixedAtBuild_PcdFixedDebugPrintErrorLevel             = _PCD_VALUE_PcdFixedDebugPrintErrorLevel;
UINT32   _gPcd_FixedAtBuild_PcdDebugPrintErrorLevel                  = _PCD_VALUE_PcdDebugPrintErrorLevel;
UINT32   _gPcd_FixedAtBuild_PcdMaximumAsciiStringLength              = _PCD_VALUE_PcdMaximumAsciiStringLength;
UINT32   _gPcd_FixedAtBuild_PcdMaximumUnicodeStringLength            = _PCD_VALUE_PcdMaximumUnicodeStringLength;
UINT32   _gPcd_FixedAtBuild_PcdMaximumLinkedListLength               = _PCD_VALUE_PcdMaximumLinkedListLength;
BOOLEAN  _gPcd_FixedAtBuild_PcdVerifyNodeInList                      = _PCD_VALUE_PcdVerifyNodeInList;
UINT32   _gPcd_FixedAtBuild_PcdCpuNumberOfReservedVariableMtrrs      = _PCD_VALUE_PcdCpuNumberOfReservedVariableMtrrs;
UINT32   _gPcd_FixedAtBuild_PcdMaximumDevicePathNodeCount            = _PCD_VALUE_PcdMaximumDevicePathNodeCount;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderRtRelocAllowTargetMismatch = FALSE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderHashProhibitOverlap        = TRUE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderLoadHeader                 = TRUE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderSupportArmThumb            = FALSE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderForceLoadDebug             = FALSE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderTolerantLoad               = TRUE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderSupportDebug               = FALSE;
BOOLEAN  _gPcd_FixedAtBuild_PcdImageLoaderRemoveXForWX               = FALSE;
UINT32   _gPcd_BinaryPatch_PcdSerialRegisterStride                   = 0;
