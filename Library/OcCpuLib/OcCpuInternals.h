/** @file
  Copyright (C) 2020, vit9696. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef OC_CPU_INTERNALS_H
#define OC_CPU_INTERNALS_H

#include <Library/OcCpuLib.h>

//
// Tolerance within which we consider two frequency values to be roughly
// equivalent.
//
#define OC_CPU_FREQUENCY_TOLERANCE  50000000LL// 50 Mhz

/**
  Internal CPU synchronisation structure.
**/
typedef struct {
  UINT64             Tsc;
  volatile UINT32    CurrentCount;
  UINT32             APThreadCount;
} OC_CPU_TSC_SYNC;

/**
  Returns microcode revision for Intel CPUs.

  @retval  microcode revision.
**/
UINT32
EFIAPI
AsmReadIntelMicrocodeRevision (
  VOID
  );

/**
  Measures TSC and ACPI ticks over specified ACPI tick amount.

  @param[in]  AcpiTicksDuration  Number of ACPI ticks for calculation.
  @param[in]  TimerAddr          ACPI timer address.
  @param[out] AcpiTicksDelta     Reported ACPI ticks delta.
  @param[out] TscTicksDelta      Reported TSC ticks delta.
**/
VOID
EFIAPI
AsmMeasureTicks (
  IN  UINT32  AcpiTicksDuration,
  IN  UINT16  TimerAddr,
  OUT UINT32  *AcpiTicksDelta,
  OUT UINT64  *TscTicksDelta
  );

/**
  Detect Apple CPU major type.

  @param[in] BrandString   CPU Brand String from CPUID.

  @retval Apple CPU major type.
**/
UINT8
InternalDetectAppleMajorType (
  IN  CONST CHAR8  *BrandString
  );

/**
  Detect Apple CPU type.

  @param[in] Model           CPU model from CPUID.
  @param[in] Stepping        CPU stepping from CPUID.
  @param[in] AppleMajorType  Apple CPU major type.
  @param[in] CoreCount       Number of physical cores.
  @param[in] Is64Bit         CPU supports 64-bit mode.

  @retval Apple CPU type.
**/
UINT16
InternalDetectAppleProcessorType (
  IN UINT8    Model,
  IN UINT8    Stepping,
  IN UINT8    AppleMajorType,
  IN UINT16   CoreCount,
  IN BOOLEAN  Is64Bit
  );

/**
  Obtain Intel CPU generation.

  @param[in] Model           CPU model from CPUID.

  @retval CPU's generation (e.g. OcCpuGenerationUnknown).
 */
OC_CPU_GENERATION
InternalDetectIntelProcessorGeneration (
  IN  OC_CPU_INFO  *CpuInfo
  );

/**
  Obtain ACPI PM timer address for this BSP.

  @param[out]  Type   Address source type, optional.

  @retval ACPI PM timer address or 0.
**/
UINTN
InternalGetPmTimerAddr (
  OUT CONST CHAR8  **Type  OPTIONAL
  );

/**
  Calculate the TSC frequency via PM timer

  @param[in] Recalculate  Do not re-use previously cached information.

  @retval  The calculated TSC frequency.
**/
UINT64
InternalCalculateTSCFromPMTimer (
  IN BOOLEAN  Recalculate
  );

/**
  Calculate the TSC frequency via Apple Platform Info

  @param[out]  FSBFrequency  Updated FSB frequency, optional.
  @param[in]   Recalculate   Do not re-use previously cached information.

  @retval  The calculated TSC frequency.
**/
UINT64
InternalCalculateTSCFromApplePlatformInfo (
  OUT  UINT64   *FSBFrequency  OPTIONAL,
  IN   BOOLEAN  Recalculate
  );

/**
  Calculate the ART frequency and derieve the CPU frequency for Intel CPUs

  @param[out] CPUFrequency  The derieved CPU frequency.
  @param[out] TscAdjustPtr  Adjustment value for TSC, optional.
  @param[in]  Recalculate   Do not re-use previously cached information.

  @retval  The calculated ART frequency.
**/
UINT64
InternalCalculateARTFrequencyIntel (
  OUT UINT64   *CPUFrequency,
  OUT UINT64   *TscAdjustPtr OPTIONAL,
  IN  BOOLEAN  Recalculate
  );

/**
  Calculate the CPU frequency via VMT for hypervisors

  @param[out] FSBFrequency     FSB frequency, optional.
  @param[out] UnderHypervisor  Hypervisor status, optional.

  @retval  CPU frequency or 0.
**/
UINT64
InternalCalculateVMTFrequency (
  OUT UINT64   *FSBFrequency     OPTIONAL,
  OUT BOOLEAN  *UnderHypervisor  OPTIONAL
  );

/**
  Convert Apple FSB frequency to TSC frequency

  @param[in]  FSBFrequency  Frequency in Apple FSB format.

  @retval  Converted TSC frequency.
**/
UINT64
InternalConvertAppleFSBToTSCFrequency (
  IN  UINT64  FSBFrequency
  );

/**
  Atomically increment 32-bit integer.
  This is required to be locally implemented as we cannot use SynchronizationLib,
  which depends on TimerLib, and our TimerLib depends on this library.

  @param[in]  Value  Pointer to 32-bit integer to increment.

  @retval value after incrementing.
**/
UINT32
EFIAPI
AsmIncrementUint32 (
  IN volatile UINT32  *Value
  );

#endif // OC_CPU_INTERNALS_H
