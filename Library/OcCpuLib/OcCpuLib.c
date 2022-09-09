/** @file
  Copyright (C) 2016 - 2017, The HermitCrabs Lab. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#include <PiDxe.h>

#include <IndustryStandard/AppleSmBios.h>
#include <Protocol/FrameworkMpService.h>
#include <Protocol/MpService.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/OcCpuLib.h>
#include <Library/OcGuardLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <IndustryStandard/ProcessorInfo.h>
#include <Register/Microcode.h>
#include <Register/Msr.h>
#include <Register/Intel/Msr/SandyBridgeMsr.h>
#include <Register/Intel/Msr/NehalemMsr.h>

#include "OcCpuInternals.h"

STATIC
EFI_STATUS
ScanMpServices (
  IN  EFI_MP_SERVICES_PROTOCOL  *MpServices,
  OUT OC_CPU_INFO               *Cpu,
  OUT UINTN                     *NumberOfProcessors,
  OUT UINTN                     *NumberOfEnabledProcessors
  )
{
  EFI_STATUS                 Status;
  UINTN                      Index;
  EFI_PROCESSOR_INFORMATION  Info;

  ASSERT (MpServices != NULL);
  ASSERT (Cpu != NULL);
  ASSERT (NumberOfProcessors != NULL);
  ASSERT (NumberOfEnabledProcessors != NULL);

  Status = MpServices->GetNumberOfProcessors (
                         MpServices,
                         NumberOfProcessors,
                         NumberOfEnabledProcessors
                         );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (*NumberOfProcessors == 0) {
    return EFI_NOT_FOUND;
  }

  //
  // This code assumes that all CPUs have same amount of cores and threads.
  //
  for (Index = 0; Index < *NumberOfProcessors; ++Index) {
    Status = MpServices->GetProcessorInfo (MpServices, Index, &Info);

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_INFO,
        "OCCPU: Failed to get info for processor %Lu - %r\n",
        (UINT64)Index,
        Status
        ));

      continue;
    }

    if (Info.Location.Package + 1 >= Cpu->PackageCount) {
      Cpu->PackageCount = (UINT16)(Info.Location.Package + 1);
    }

    if (Info.Location.Core + 1 >= Cpu->CoreCount) {
      Cpu->CoreCount = (UINT16)(Info.Location.Core + 1);
    }

    if (Info.Location.Thread + 1 >= Cpu->ThreadCount) {
      Cpu->ThreadCount = (UINT16)(Info.Location.Thread + 1);
    }
  }

  return Status;
}

STATIC
EFI_STATUS
ScanFrameworkMpServices (
  IN  FRAMEWORK_EFI_MP_SERVICES_PROTOCOL  *FrameworkMpServices,
  OUT OC_CPU_INFO                         *Cpu,
  OUT UINTN                               *NumberOfProcessors,
  OUT UINTN                               *NumberOfEnabledProcessors
  )
{
  EFI_STATUS           Status;
  UINTN                Index;
  EFI_MP_PROC_CONTEXT  Context;
  UINTN                ContextSize;

  ASSERT (FrameworkMpServices != NULL);
  ASSERT (Cpu != NULL);
  ASSERT (NumberOfProcessors != NULL);
  ASSERT (NumberOfEnabledProcessors != NULL);

  Status = FrameworkMpServices->GetGeneralMPInfo (
                                  FrameworkMpServices,
                                  NumberOfProcessors,
                                  NULL,
                                  NumberOfEnabledProcessors,
                                  NULL,
                                  NULL
                                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (*NumberOfProcessors == 0) {
    return EFI_NOT_FOUND;
  }

  //
  // This code assumes that all CPUs have same amount of cores and threads.
  //
  for (Index = 0; Index < *NumberOfProcessors; ++Index) {
    ContextSize = sizeof (Context);

    Status = FrameworkMpServices->GetProcessorContext (
                                    FrameworkMpServices,
                                    Index,
                                    &ContextSize,
                                    &Context
                                    );

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_INFO,
        "OCCPU: Failed to get context for processor %Lu - %r\n",
        (UINT64)Index,
        Status
        ));

      continue;
    }

    if (Context.PackageNumber + 1 >= Cpu->PackageCount) {
      Cpu->PackageCount = (UINT16)(Context.PackageNumber + 1);
    }

    //
    // According to the FrameworkMpServices header, EFI_MP_PROC_CONTEXT.NumberOfCores is the
    // zero-indexed physical core number for the current processor. However, Apple appears to
    // set this to the total number of physical cores (observed on Xserve3,1 with 2x Xeon X5550).
    //
    // This number may not be accurate; on MacPro5,1 with 2x Xeon X5690, NumberOfCores is 16 when
    // it should be 12 (even though NumberOfProcessors is correct). Regardless, CoreCount and
    // ThreadCount will be corrected in ScanIntelProcessor.
    //
    // We will follow Apple's implementation, as the FrameworkMpServices fallback was added for
    // legacy Macs.
    //
    if (Context.NumberOfCores >= Cpu->CoreCount) {
      Cpu->CoreCount = (UINT16)(Context.NumberOfCores);
    }

    //
    // Similarly, EFI_MP_PROC_CONTEXT.NumberOfThreads is supposed to be the zero-indexed logical
    // thread number for the current processor. On Xserve3,1 and MacPro5,1 this was set to 2
    // (presumably to indicate that there are 2 threads per physical core).
    //
    if (Context.NumberOfCores * Context.NumberOfThreads >= Cpu->ThreadCount) {
      Cpu->ThreadCount = (UINT16)(Context.NumberOfCores * Context.NumberOfThreads);
    }
  }

  return Status;
}

STATIC
EFI_STATUS
ScanThreadCount (
  OUT OC_CPU_INFO  *Cpu
  )
{
  EFI_STATUS                          Status;
  EFI_MP_SERVICES_PROTOCOL            *MpServices;
  FRAMEWORK_EFI_MP_SERVICES_PROTOCOL  *FrameworkMpServices;
  UINTN                               NumberOfProcessors;
  UINTN                               NumberOfEnabledProcessors;

  Cpu->PackageCount         = 1;
  Cpu->CoreCount            = 1;
  Cpu->ThreadCount          = 1;
  NumberOfProcessors        = 0;
  NumberOfEnabledProcessors = 0;

  Status = gBS->LocateProtocol (
                  &gEfiMpServiceProtocolGuid,
                  NULL,
                  (VOID **)&MpServices
                  );

  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (
                    &gFrameworkEfiMpServiceProtocolGuid,
                    NULL,
                    (VOID **)&FrameworkMpServices
                    );

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "OCCPU: No MP services - %r\n", Status));
      return Status;
    }

    Status = ScanFrameworkMpServices (
               FrameworkMpServices,
               Cpu,
               &NumberOfProcessors,
               &NumberOfEnabledProcessors
               );
  } else {
    Status = ScanMpServices (
               MpServices,
               Cpu,
               &NumberOfProcessors,
               &NumberOfEnabledProcessors
               );
  }

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: MP services threads %Lu (enabled %Lu) - %r\n",
    (UINT64)NumberOfProcessors,
    (UINT64)NumberOfEnabledProcessors,
    Status
    ));

  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: MP services Pkg %u Cores %u Threads %u - %r\n",
    Cpu->PackageCount,
    Cpu->CoreCount,
    Cpu->ThreadCount,
    Status
    ));

  //
  // Several implementations may not report virtual threads.
  //
  if (Cpu->ThreadCount < Cpu->CoreCount) {
    Cpu->ThreadCount = Cpu->CoreCount;
  }

  return Status;
}

STATIC
VOID
SetMaxBusRatioAndMaxBusRatioDiv (
  IN   OC_CPU_INFO  *CpuInfo  OPTIONAL,
  OUT  UINT8        *MaxBusRatio,
  OUT  UINT8        *MaxBusRatioDiv
  )
{
  MSR_IA32_PERF_STATUS_REGISTER       PerfStatus;
  MSR_NEHALEM_PLATFORM_INFO_REGISTER  PlatformInfo;
  CPUID_VERSION_INFO_EAX              Eax;
  UINT8                               CpuModel;

  ASSERT (MaxBusRatio != NULL);
  ASSERT (MaxBusRatioDiv != NULL);

  if (CpuInfo != NULL) {
    CpuModel = CpuInfo->Model;
  } else {
    //
    // Assuming Intel machines used on Apple hardware.
    //
    AsmCpuid (
      CPUID_VERSION_INFO,
      &Eax.Uint32,
      NULL,
      NULL,
      NULL
      );
    CpuModel = (UINT8)Eax.Bits.Model | (UINT8)(Eax.Bits.ExtendedModelId << 4U);
  }

  //
  // Refer to Intel SDM (MSRs in Processors Based on Intel... table).
  //
  if ((CpuModel >= CPU_MODEL_NEHALEM) && (CpuModel != CPU_MODEL_BONNELL)) {
    PlatformInfo.Uint64 = AsmReadMsr64 (MSR_NEHALEM_PLATFORM_INFO);
    *MaxBusRatio        = (UINT8)PlatformInfo.Bits.MaximumNonTurboRatio;
    *MaxBusRatioDiv     = 0;
  } else {
    PerfStatus.Uint64 = AsmReadMsr64 (MSR_IA32_PERF_STATUS);
    *MaxBusRatio      = (UINT8)(RShiftU64 (PerfStatus.Uint64, 40) & 0x1FU);
    *MaxBusRatioDiv   = (UINT8)(RShiftU64 (PerfStatus.Uint64, 46) & BIT0);
  }

  //
  // Fall back to 1 if *MaxBusRatio has zero.
  //
  if (*MaxBusRatio == 0) {
    *MaxBusRatio = 1;
  }
}

STATIC
VOID
ScanIntelFSBFrequency (
  IN  OC_CPU_INFO  *CpuInfo
  )
{
  UINT8  MaxBusRatio;
  UINT8  MaxBusRatioDiv;

  ASSERT (CpuInfo != NULL);

  //
  // Do not reset if CpuInfo->FSBFrequency is already set.
  //
  if (CpuInfo->FSBFrequency > 0) {
    return;
  }

  SetMaxBusRatioAndMaxBusRatioDiv (CpuInfo, &MaxBusRatio, &MaxBusRatioDiv);

  //
  // There may be some quirks with virtual CPUs (VMware is fine).
  // Formerly we checked Cpu->MinBusRatio > 0, and MaxBusRatio falls back to 1 if it is 0.
  //
  if (CpuInfo->CPUFrequency > 0) {
    if (MaxBusRatioDiv == 0) {
      CpuInfo->FSBFrequency = DivU64x32 (CpuInfo->CPUFrequency, MaxBusRatio);
    } else {
      CpuInfo->FSBFrequency = MultThenDivU64x64x32 (
                                CpuInfo->CPUFrequency,
                                2,
                                2 * MaxBusRatio + 1,
                                NULL
                                );
    }
  } else {
    //
    // TODO: It seems to be possible that CPU frequency == 0 here...
    //
    CpuInfo->FSBFrequency = 100000000; // 100 MHz
  }

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: Intel TSC: %11LuHz, %5LuMHz; FSB: %11LuHz, %5LuMHz; MaxBusRatio: %u%a\n",
    CpuInfo->CPUFrequency,
    DivU64x32 (CpuInfo->CPUFrequency, 1000000),
    CpuInfo->FSBFrequency,
    DivU64x32 (CpuInfo->FSBFrequency, 1000000),
    MaxBusRatio,
    MaxBusRatioDiv != 0 ? ".5" : ""
    ));
}

UINT64
InternalConvertAppleFSBToTSCFrequency (
  IN  UINT64  FSBFrequency
  )
{
  UINT8  MaxBusRatio;
  UINT8  MaxBusRatioDiv;

  SetMaxBusRatioAndMaxBusRatioDiv (NULL, &MaxBusRatio, &MaxBusRatioDiv);

  //
  // When MaxBusRatioDiv is 1, the multiplier is MaxBusRatio + 0.5.
  //
  if (MaxBusRatioDiv == 1) {
    return FSBFrequency * MaxBusRatio + FSBFrequency / 2;
  }

  return FSBFrequency * MaxBusRatio;
}

STATIC
VOID
ScanIntelProcessorApple (
  IN OUT OC_CPU_INFO  *Cpu
  )
{
  UINT8  AppleMajorType;

  AppleMajorType          = InternalDetectAppleMajorType (Cpu->BrandString);
  Cpu->AppleProcessorType = InternalDetectAppleProcessorType (
                              Cpu->Model,
                              Cpu->Stepping,
                              AppleMajorType,
                              Cpu->CoreCount,
                              (Cpu->ExtFeatures & CPUID_EXTFEATURE_EM64T) != 0
                              );

  DEBUG ((DEBUG_INFO, "OCCPU: Detected Apple Processor Type: %02X -> %04X\n", AppleMajorType, Cpu->AppleProcessorType));
}

STATIC
VOID
ScanIntelProcessor (
  IN OUT OC_CPU_INFO  *Cpu
  )
{
  UINT64                                            Msr;
  CPUID_CACHE_PARAMS_EAX                            CpuidCacheEax;
  CPUID_CACHE_PARAMS_EBX                            CpuidCacheEbx;
  MSR_SANDY_BRIDGE_PKG_CST_CONFIG_CONTROL_REGISTER  PkgCstConfigControl;
  UINT16                                            CoreCount;
  CONST CHAR8                                       *TimerSourceType;
  UINTN                                             TimerAddr;
  BOOLEAN                                           Recalculate;

  if (  ((Cpu->Family != 0x06) || (Cpu->Model < 0x0c))
     && ((Cpu->Family != 0x0f) || (Cpu->Model < 0x03)))
  {
    ScanIntelProcessorApple (Cpu);
    return;
  }

  Cpu->CpuGeneration = InternalDetectIntelProcessorGeneration (Cpu);

  //
  // Some virtual machines like QEMU 5.0 with KVM will fail to read this value.
  // REF: https://github.com/acidanthera/bugtracker/issues/914
  //
  if ((Cpu->CpuGeneration >= OcCpuGenerationSandyBridge) && !Cpu->Hypervisor) {
    PkgCstConfigControl.Uint64 = AsmReadMsr64 (MSR_SANDY_BRIDGE_PKG_CST_CONFIG_CONTROL);
    Cpu->CstConfigLock         = PkgCstConfigControl.Bits.CFGLock == 1;
  } else {
    Cpu->CstConfigLock = FALSE;
  }

  DEBUG ((DEBUG_INFO, "OCCPU: EIST CFG Lock %d\n", Cpu->CstConfigLock));

  //
  // When the CPU is virtualized and cpuid invtsc is enabled, then we already get
  // the information we want outside the function, skip anyway.
  // Things may be different in other hypervisors, but should work with QEMU/VMWare for now.
  //
  if (Cpu->CPUFrequencyFromVMT == 0) {
    //
    // For logging purposes (the first call to these functions might happen
    // before logging is fully initialised), do not use the cached results in
    // DEBUG builds.
    //
    Recalculate = FALSE;

    DEBUG_CODE_BEGIN ();
    Recalculate = TRUE;
    DEBUG_CODE_END ();

    //
    // Determine our core crystal clock frequency
    //
    Cpu->ARTFrequency = InternalCalculateARTFrequencyIntel (
                          &Cpu->CPUFrequencyFromART,
                          &Cpu->TscAdjust,
                          Recalculate
                          );

    //
    // Calculate the TSC frequency only if ART frequency is not available or we are in debug builds.
    //
    if ((Cpu->CPUFrequencyFromART == 0) || Recalculate) {
      DEBUG_CODE_BEGIN ();
      TimerAddr = InternalGetPmTimerAddr (&TimerSourceType);
      DEBUG ((DEBUG_INFO, "OCCPU: Timer address is %Lx from %a\n", (UINT64)TimerAddr, TimerSourceType));
      DEBUG_CODE_END ();
      Cpu->CPUFrequencyFromApple = InternalCalculateTSCFromApplePlatformInfo (NULL, Recalculate);
      if ((Cpu->CPUFrequencyFromApple == 0) || Recalculate) {
        Cpu->CPUFrequencyFromTSC = InternalCalculateTSCFromPMTimer (Recalculate);
      }
    }

    //
    // Calculate CPU frequency firstly based on ART if present.
    //
    if (Cpu->CPUFrequencyFromART != 0) {
      Cpu->CPUFrequency = Cpu->CPUFrequencyFromART;
    } else {
      //
      // If ART is not available, then try the value from Apple Platform Info.
      //
      if (Cpu->CPUFrequencyFromApple != 0) {
        Cpu->CPUFrequency = Cpu->CPUFrequencyFromApple;
      } else {
        //
        // If still not available, finally use TSC.
        //
        Cpu->CPUFrequency = Cpu->CPUFrequencyFromTSC;
      }
    }

    //
    // Verify that ART/TSC CPU frequency calculations do not differ substantially.
    //
    if (  (Cpu->CPUFrequencyFromART > 0) && (Cpu->CPUFrequencyFromTSC > 0)
       && (ABS ((INT64)Cpu->CPUFrequencyFromART - (INT64)Cpu->CPUFrequencyFromTSC) > OC_CPU_FREQUENCY_TOLERANCE))
    {
      DEBUG ((
        DEBUG_WARN,
        "OCCPU: ART based CPU frequency differs substantially from TSC: %11LuHz != %11LuHz\n",
        Cpu->CPUFrequencyFromART,
        Cpu->CPUFrequencyFromTSC
        ));
    }

    //
    // Verify that Apple/TSC CPU frequency calculations do not differ substantially.
    //
    if (  (Cpu->CPUFrequencyFromApple > 0) && (Cpu->CPUFrequencyFromTSC > 0)
       && (ABS ((INT64)Cpu->CPUFrequencyFromApple - (INT64)Cpu->CPUFrequencyFromTSC) > OC_CPU_FREQUENCY_TOLERANCE))
    {
      DEBUG ((
        DEBUG_WARN,
        "OCCPU: Apple based CPU frequency differs substantially from TSC: %11LuHz != %11LuHz\n",
        Cpu->CPUFrequencyFromApple,
        Cpu->CPUFrequencyFromTSC
        ));
    }

    ScanIntelFSBFrequency (Cpu);
  }

  //
  // Calculate number of cores.
  // If we are under virtualization, then we should get the topology from CPUID the same was as with Penryn.
  //
  if (  (Cpu->MaxId >= CPUID_CACHE_PARAMS)
     && (  (Cpu->CpuGeneration == OcCpuGenerationPreYonah)
        || (Cpu->CpuGeneration == OcCpuGenerationYonahMerom)
        || (Cpu->CpuGeneration == OcCpuGenerationPenryn)
        || (Cpu->CpuGeneration == OcCpuGenerationBonnell)
        || (Cpu->CpuGeneration == OcCpuGenerationSilvermont)
        || Cpu->Hypervisor))
  {
    AsmCpuidEx (CPUID_CACHE_PARAMS, 0, &CpuidCacheEax.Uint32, &CpuidCacheEbx.Uint32, NULL, NULL);
    if (CpuidCacheEax.Bits.CacheType != CPUID_CACHE_PARAMS_CACHE_TYPE_NULL) {
      CoreCount = (UINT16)GetPowerOfTwo32 (CpuidCacheEax.Bits.MaximumAddressableIdsForProcessorCores + 1);
      if (CoreCount < CpuidCacheEax.Bits.MaximumAddressableIdsForProcessorCores + 1) {
        CoreCount *= 2;
      }

      Cpu->CoreCount = CoreCount;
      //
      // We should not be blindly relying on Cpu->Features & CPUID_FEATURE_HTT.
      // On Penryn CPUs it is set even without Hyper Threading.
      //
      if (Cpu->ThreadCount < Cpu->CoreCount) {
        Cpu->ThreadCount = Cpu->CoreCount;
      }
    }
  } else if (Cpu->CpuGeneration == OcCpuGenerationWestmere) {
    Msr              = AsmReadMsr64 (MSR_CORE_THREAD_COUNT);
    Cpu->CoreCount   = (UINT16)BitFieldRead64 (Msr, 16, 19);
    Cpu->ThreadCount = (UINT16)BitFieldRead64 (Msr, 0, 15);
  } else if (Cpu->CpuGeneration == OcCpuGenerationBanias) {
    //
    // Banias and Dothan (Pentium M and Celeron M) never had
    // multiple cores or threads, and do not support the MSR below.
    //
    Cpu->CoreCount   = 0;
    Cpu->ThreadCount = 0;
  } else if (  (Cpu->CpuGeneration == OcCpuGenerationPreYonah)
            && (Cpu->MaxId < CPUID_CACHE_PARAMS))
  {
    //
    // Legacy Pentium 4, e.g. 541.
    // REF: https://github.com/acidanthera/bugtracker/issues/1783
    //
    Cpu->CoreCount = 1;
  } else {
    Msr              = AsmReadMsr64 (MSR_CORE_THREAD_COUNT);
    Cpu->CoreCount   = (UINT16)BitFieldRead64 (Msr, 16, 31);
    Cpu->ThreadCount = (UINT16)BitFieldRead64 (Msr, 0, 15);
  }

  if (Cpu->CoreCount == 0) {
    Cpu->CoreCount = 1;
  }

  if (Cpu->ThreadCount == 0) {
    Cpu->ThreadCount = 1;
  }

  ScanIntelProcessorApple (Cpu);
}

STATIC
VOID
ScanAmdProcessor (
  IN OUT OC_CPU_INFO  *Cpu
  )
{
  UINT32   CpuidEbx;
  UINT32   CpuidEcx;
  UINT64   CofVid;
  UINT8    CoreFrequencyID;
  UINT8    CoreDivisorID;
  UINT8    Divisor;
  UINT8    MaxBusRatio;
  BOOLEAN  Recalculate;

  //
  // For logging purposes (the first call to these functions might happen
  // before logging is fully initialised), do not use the cached results in
  // DEBUG builds.
  //
  Recalculate = FALSE;

  DEBUG_CODE_BEGIN ();
  Recalculate = TRUE;
  DEBUG_CODE_END ();

  //
  // get TSC Frequency calculated in OcTimerLib, unless we got it already from virtualization extensions.
  // FIXME(1): This code assumes the CPU operates in P0.  Either ensure it does
  //           and raise the mode on demand, or adapt the logic to consider
  //           both the operating and the nominal frequency, latter for
  //           the invariant TSC.
  //
  if (Cpu->CPUFrequencyFromVMT == 0) {
    Cpu->CPUFrequencyFromTSC = InternalCalculateTSCFromPMTimer (Recalculate);
    Cpu->CPUFrequency        = Cpu->CPUFrequencyFromTSC;
  }

  //
  // Get core and thread count from CPUID
  //
  if (Cpu->MaxExtId >= 0x80000008) {
    AsmCpuid (0x80000008, NULL, NULL, &CpuidEcx, NULL);
    Cpu->ThreadCount = (UINT16)(BitFieldRead32 (CpuidEcx, 0, 7) + 1);
  }

  //
  // Faking an Intel processor with matching core count if possible.
  // This value is purely cosmetic, but it makes sense to fake something
  // that is somewhat representative of the kind of Processor that's actually
  // in the system
  //
  if (Cpu->ThreadCount >= 8) {
    Cpu->AppleProcessorType = AppleProcessorTypeXeonW;
  } else {
    Cpu->AppleProcessorType = AppleProcessorTypeCorei5Type5;
  }

  if (Cpu->Family == AMD_CPU_FAMILY) {
    Divisor         = 0;
    CoreFrequencyID = 0;
    CoreDivisorID   = 0;
    MaxBusRatio     = 0;

    switch (Cpu->ExtFamily) {
      case AMD_CPU_EXT_FAMILY_17H:
      case AMD_CPU_EXT_FAMILY_19H:
        if (Cpu->CPUFrequencyFromVMT == 0) {
          CofVid          = AsmReadMsr64 (K10_PSTATE_STATUS);
          CoreFrequencyID = (UINT8)BitFieldRead64 (CofVid, 0, 7);
          CoreDivisorID   = (UINT8)BitFieldRead64 (CofVid, 8, 13);
          if (CoreDivisorID > 0ULL) {
            //
            // Sometimes incorrect hypervisor configuration will lead to dividing by zero,
            // but these variables will not be used under hypervisor, so just skip these.
            //
            MaxBusRatio = (UINT8)(CoreFrequencyID / CoreDivisorID * 2);
          }
        }

        //
        // Get core count from CPUID
        //
        if (Cpu->MaxExtId >= 0x8000001E) {
          AsmCpuid (0x8000001E, NULL, &CpuidEbx, NULL, NULL);
          Cpu->CoreCount = (UINT16)DivU64x32 (
                                     Cpu->ThreadCount,
                                     (BitFieldRead32 (CpuidEbx, 8, 15) + 1)
                                     );
        }

        break;
      case AMD_CPU_EXT_FAMILY_15H:
      case AMD_CPU_EXT_FAMILY_16H:
        if (Cpu->CPUFrequencyFromVMT == 0) {
          // FIXME: Please refer to FIXME(1) for the MSR used here.
          CofVid          = AsmReadMsr64 (K10_COFVID_STATUS);
          CoreFrequencyID = (UINT8)BitFieldRead64 (CofVid, 0, 5);
          CoreDivisorID   = (UINT8)BitFieldRead64 (CofVid, 6, 8);
          Divisor         = 1U << CoreDivisorID;
          //
          // BKDG for AMD Family 15h Models 10h-1Fh Processors (42300 Rev 3.12)
          // Core current operating frequency in MHz. CoreCOF = 100 *
          // (MSRC001_00[6B:64][CpuFid] + 10h) / (2 ^ MSRC001_00[6B:64][CpuDid]).
          //
          if (Divisor > 0ULL) {
            //
            // Sometimes incorrect hypervisor configuration will lead to dividing by zero,
            // but these variables will not be used under hypervisor, so just skip these.
            //
            MaxBusRatio = (UINT8)((CoreFrequencyID + 0x10) / Divisor);
          }
        }

        //
        // AMD 15h and 16h CPUs don't support hyperthreading,
        // so the core count is equal to the thread count
        //
        Cpu->CoreCount = Cpu->ThreadCount;
        break;
      default:
        return;
    }

    DEBUG ((
      DEBUG_INFO,
      "OCCPU: FID %u DID %u Divisor %u MaxBR %u\n",
      CoreFrequencyID,
      CoreDivisorID,
      Divisor,
      MaxBusRatio
      ));

    //
    // When under virtualization this information is already available to us.
    //
    if (Cpu->CPUFrequencyFromVMT == 0) {
      //
      // Sometimes incorrect hypervisor configuration will lead to dividing by zero.
      //
      if (MaxBusRatio == 0) {
        Cpu->FSBFrequency = 100000000; // 100 MHz like Intel part.
      } else {
        Cpu->FSBFrequency = DivU64x32 (Cpu->CPUFrequency, MaxBusRatio);
      }
    }
  }
}

/**
  Scan the processor and fill the cpu info structure with results.

  @param[in,out] Cpu  A pointer to the cpu info structure to fill with results.
**/
VOID
OcCpuScanProcessor (
  IN OUT OC_CPU_INFO  *Cpu
  )
{
  UINT32  CpuidEax;
  UINT32  CpuidEbx;
  UINT32  CpuidEcx;
  UINT32  CpuidEdx;

  ASSERT (Cpu != NULL);

  ZeroMem (Cpu, sizeof (*Cpu));

  //
  // Get vendor CPUID 0x00000000
  //
  AsmCpuid (CPUID_SIGNATURE, &CpuidEax, &Cpu->Vendor[0], &Cpu->Vendor[2], &Cpu->Vendor[1]);

  Cpu->MaxId = CpuidEax;

  //
  // Get extended CPUID 0x80000000
  //
  AsmCpuid (CPUID_EXTENDED_FUNCTION, &CpuidEax, &CpuidEbx, &CpuidEcx, &CpuidEdx);

  Cpu->MaxExtId = CpuidEax;

  //
  // Get brand string CPUID 0x80000002 - 0x80000004
  //
  if (Cpu->MaxExtId >= CPUID_BRAND_STRING3) {
    //
    // The brandstring 48 bytes max, guaranteed NULL terminated.
    //
    UINT32  *BrandString = (UINT32 *)Cpu->BrandString;

    AsmCpuid (
      CPUID_BRAND_STRING1,
      BrandString,
      (BrandString + 1),
      (BrandString + 2),
      (BrandString + 3)
      );

    AsmCpuid (
      CPUID_BRAND_STRING2,
      (BrandString + 4),
      (BrandString + 5),
      (BrandString + 6),
      (BrandString + 7)
      );

    AsmCpuid (
      CPUID_BRAND_STRING3,
      (BrandString + 8),
      (BrandString + 9),
      (BrandString + 10),
      (BrandString + 11)
      );
  }

  ScanThreadCount (Cpu);

  //
  // Get processor signature and decode
  //
  if (Cpu->MaxId >= CPUID_VERSION_INFO) {
    if (Cpu->Vendor[0] == CPUID_VENDOR_INTEL) {
      Cpu->MicrocodeRevision = AsmReadIntelMicrocodeRevision ();
    }

    AsmCpuid (
      CPUID_VERSION_INFO,
      &Cpu->CpuidVerEax.Uint32,
      &Cpu->CpuidVerEbx.Uint32,
      &Cpu->CpuidVerEcx.Uint32,
      &Cpu->CpuidVerEdx.Uint32
      );

    Cpu->Signature = Cpu->CpuidVerEax.Uint32;
    Cpu->Stepping  = (UINT8)Cpu->CpuidVerEax.Bits.SteppingId;
    Cpu->ExtModel  = (UINT8)Cpu->CpuidVerEax.Bits.ExtendedModelId;
    Cpu->Model     = (UINT8)Cpu->CpuidVerEax.Bits.Model | (UINT8)(Cpu->CpuidVerEax.Bits.ExtendedModelId << 4U);
    Cpu->Family    = (UINT8)Cpu->CpuidVerEax.Bits.FamilyId;
    Cpu->Type      = (UINT8)Cpu->CpuidVerEax.Bits.ProcessorType;
    Cpu->ExtFamily = (UINT8)Cpu->CpuidVerEax.Bits.ExtendedFamilyId;
    Cpu->Brand     = (UINT8)Cpu->CpuidVerEbx.Bits.BrandIndex;
    Cpu->Features  = LShiftU64 (Cpu->CpuidVerEcx.Uint32, 32) | Cpu->CpuidVerEdx.Uint32;

    if (Cpu->Features & CPUID_FEATURE_HTT) {
      Cpu->ThreadCount = (UINT16)Cpu->CpuidVerEbx.Bits.MaximumAddressableIdsForLogicalProcessors;
    }
  }

  //
  // Get extended processor signature.
  //
  if (Cpu->MaxExtId >= CPUID_EXTENDED_CPU_SIG) {
    AsmCpuid (
      CPUID_EXTENDED_CPU_SIG,
      &CpuidEax,
      &CpuidEbx,
      &Cpu->CpuidExtSigEcx.Uint32,
      &Cpu->CpuidExtSigEdx.Uint32
      );

    Cpu->ExtFeatures = LShiftU64 (Cpu->CpuidExtSigEcx.Uint32, 32) | Cpu->CpuidExtSigEdx.Uint32;
  }

  DEBUG ((DEBUG_INFO, "OCCPU: Found %a\n", Cpu->BrandString));

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: Signature %0X Stepping %0X Model %0X Family %0X Type %0X ExtModel %0X ExtFamily %0X uCode %0X CPUID MAX (%0X/%0X)\n",
    Cpu->Signature,
    Cpu->Stepping,
    Cpu->Model,
    Cpu->Family,
    Cpu->Type,
    Cpu->ExtModel,
    Cpu->ExtFamily,
    Cpu->MicrocodeRevision,
    Cpu->MaxId,
    Cpu->MaxExtId
    ));

  Cpu->CPUFrequencyFromVMT = InternalCalculateVMTFrequency (
                               &Cpu->FSBFrequency,
                               &Cpu->Hypervisor
                               );

  if (Cpu->Hypervisor) {
    DEBUG ((DEBUG_INFO, "OCCPU: Hypervisor detected\n"));
  }

  if (Cpu->CPUFrequencyFromVMT > 0) {
    Cpu->CPUFrequency = Cpu->CPUFrequencyFromVMT;

    DEBUG ((
      DEBUG_INFO,
      "OCCPU: VMWare TSC: %11LuHz, %5LuMHz; FSB: %11LuHz, %5LuMHz\n",
      Cpu->CPUFrequency,
      DivU64x32 (Cpu->CPUFrequency, 1000000),
      Cpu->FSBFrequency,
      DivU64x32 (Cpu->FSBFrequency, 1000000)
      ));
  }

  if (Cpu->Vendor[0] == CPUID_VENDOR_INTEL) {
    ScanIntelProcessor (Cpu);
  } else if (Cpu->Vendor[0] == CPUID_VENDOR_AMD) {
    ScanAmdProcessor (Cpu);
  } else {
    DEBUG ((DEBUG_WARN, " unsupported CPU vendor: %0X", Cpu->Vendor[0]));
    return;
  }

  //
  // SMBIOS Type4 ExternalClock field is assumed to have X*4 FSB frequency in MT/s.
  // This value is cosmetics, but we still want to set it properly.
  // Magic 4 value comes from 4 links in pretty much every modern Intel CPU.
  // On modern CPUs this is now named Base clock (BCLK).
  // Note, that this value was incorrect for most Macs since iMac12,x till iMac18,x inclusive.
  // REF: https://github.com/acidanthera/bugtracker/issues/622#issuecomment-570811185
  //
  Cpu->ExternalClock = (UINT16)DivU64x32 (Cpu->FSBFrequency, 1000000);
  //
  // This is again cosmetics by errors in FSBFrequency calculation.
  //
  if ((Cpu->ExternalClock >= 99) && (Cpu->ExternalClock <= 101)) {
    Cpu->ExternalClock = 100;
  } else if ((Cpu->ExternalClock >= 132) && (Cpu->ExternalClock <= 134)) {
    Cpu->ExternalClock = 133;
  } else if ((Cpu->ExternalClock >= 265) && (Cpu->ExternalClock <= 267)) {
    Cpu->ExternalClock = 266;
  }

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: CPUFrequencyFromTSC %11LuHz %5LuMHz\n",
    Cpu->CPUFrequencyFromTSC,
    DivU64x32 (Cpu->CPUFrequencyFromTSC, 1000000)
    ));

  if (Cpu->CPUFrequencyFromApple > 0) {
    DEBUG ((
      DEBUG_INFO,
      "OCCPU: CPUFrequencyFromApple %11LuHz %5LuMHz\n",
      Cpu->CPUFrequencyFromApple,
      DivU64x32 (Cpu->CPUFrequencyFromApple, 1000000)
      ));
  }

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: CPUFrequency %11LuHz %5LuMHz\n",
    Cpu->CPUFrequency,
    DivU64x32 (Cpu->CPUFrequency, 1000000)
    ));

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: FSBFrequency %11LuHz %5LuMHz\n",
    Cpu->FSBFrequency,
    DivU64x32 (Cpu->FSBFrequency, 1000000)
    ));

  DEBUG ((
    DEBUG_INFO,
    "OCCPU: Pkg %u Cores %u Threads %u\n",
    Cpu->PackageCount,
    Cpu->CoreCount,
    Cpu->ThreadCount
    ));
}

VOID
OcCpuGetMsrReport (
  IN  OC_CPU_INFO        *CpuInfo,
  OUT OC_CPU_MSR_REPORT  *Report
  )
{
  ASSERT (CpuInfo != NULL);
  ASSERT (Report  != NULL);

  ZeroMem (Report, sizeof (*Report));

  //
  // The CPU model must be Intel, as MSRs are not available on other platforms.
  //
  if (CpuInfo->Vendor[0] != CPUID_VENDOR_INTEL) {
    return;
  }

  if (CpuInfo->CpuGeneration >= OcCpuGenerationNehalem) {
    //
    // MSR_PLATFORM_INFO
    //
    Report->CpuHasMsrPlatformInfo   = TRUE;
    Report->CpuMsrPlatformInfoValue = AsmReadMsr64 (MSR_NEHALEM_PLATFORM_INFO);

    //
    // MSR_TURBO_RATIO_LIMIT
    //
    Report->CpuHasMsrTurboRatioLimit   = TRUE;
    Report->CpuMsrTurboRatioLimitValue = AsmReadMsr64 (MSR_NEHALEM_TURBO_RATIO_LIMIT);

    if (CpuInfo->CpuGeneration >= OcCpuGenerationSandyBridge) {
      //
      // MSR_PKG_POWER_INFO (TODO: To be confirmed)
      //
      Report->CpuHasMsrPkgPowerInfo   = TRUE;
      Report->CpuMsrPkgPowerInfoValue = AsmReadMsr64 (MSR_GOLDMONT_PKG_POWER_INFO);

      //
      // MSR_BROADWELL_PKG_CST_CONFIG_CONTROL_REGISTER (MSR 0xE2)
      //
      Report->CpuHasMsrE2   = TRUE;
      Report->CpuMsrE2Value = AsmReadMsr64 (MSR_BROADWELL_PKG_CST_CONFIG_CONTROL);
    }
  } else if (CpuInfo->CpuGeneration >= OcCpuGenerationPreYonah) {
    if (CpuInfo->CpuGeneration >= OcCpuGenerationYonahMerom) {
      //
      // MSR_IA32_EXT_CONFIG
      //
      Report->CpuHasMsrIa32ExtConfig   = TRUE;
      Report->CpuMsrIa32ExtConfigValue = AsmReadMsr64 (MSR_IA32_EXT_CONFIG);

      //
      // MSR_CORE_FSB_FREQ
      //
      Report->CpuHasMsrFsbFreq   = TRUE;
      Report->CpuMsrFsbFreqValue = AsmReadMsr64 (MSR_CORE_FSB_FREQ);
    }

    //
    // MSR_IA32_MISC_ENABLE
    //
    Report->CpuHasMsrIa32MiscEnable   = TRUE;
    Report->CpuMsrIa32MiscEnableValue = AsmReadMsr64 (MSR_IA32_MISC_ENABLE);

    //
    // MSR_IA32_PERF_STATUS
    //
    Report->CpuHasMsrIa32PerfStatus   = TRUE;
    Report->CpuMsrIa32PerfStatusValue = AsmReadMsr64 (MSR_IA32_PERF_STATUS);
  }
}

VOID
EFIAPI
OcCpuGetMsrReportPerCore (
  IN OUT VOID  *Buffer
  )
{
  OC_CPU_MSR_REPORT_PROCEDURE_ARGUMENT  *Argument;
  EFI_STATUS                            Status;
  UINTN                                 CoreIndex;

  Argument = (OC_CPU_MSR_REPORT_PROCEDURE_ARGUMENT *)Buffer;

  Status = Argument->MpServices->WhoAmI (
                                   Argument->MpServices,
                                   &CoreIndex
                                   );
  if (EFI_ERROR (Status)) {
    return;
  }

  OcCpuGetMsrReport (Argument->CpuInfo, &Argument->Reports[CoreIndex]);
}

OC_CPU_MSR_REPORT *
OcCpuGetMsrReports (
  IN  OC_CPU_INFO  *CpuInfo,
  OUT UINTN        *EntryCount
  )
{
  OC_CPU_MSR_REPORT                     *Reports;
  EFI_STATUS                            Status;
  EFI_MP_SERVICES_PROTOCOL              *MpServices;
  UINTN                                 NumberOfProcessors;
  UINTN                                 NumberOfEnabledProcessors;
  OC_CPU_MSR_REPORT_PROCEDURE_ARGUMENT  Argument;

  ASSERT (CpuInfo    != NULL);
  ASSERT (EntryCount != NULL);

  MpServices = NULL;

  Status = gBS->LocateProtocol (
                  &gEfiMpServiceProtocolGuid,
                  NULL,
                  (VOID **)&MpServices
                  );
  if (!EFI_ERROR (Status)) {
    Status = MpServices->GetNumberOfProcessors (
                           MpServices,
                           &NumberOfProcessors,
                           &NumberOfEnabledProcessors
                           );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "OCCPU: Failed to get the number of processors - %r, assuming one core\n", Status));
      NumberOfProcessors = 1;
    }
  } else {
    DEBUG ((DEBUG_INFO, "OCCPU: Failed to find mp services - %r, assuming one core\n", Status));
    MpServices         = NULL;
    NumberOfProcessors = 1;
  }

  Reports = (OC_CPU_MSR_REPORT *)AllocateZeroPool (NumberOfProcessors * sizeof (OC_CPU_MSR_REPORT));
  if (Reports == NULL) {
    return NULL;
  }

  //
  // Call OcCpuGetMsrReport on the 0th member firstly.
  //
  OcCpuGetMsrReport (CpuInfo, &Reports[0]);
  //
  // Then call StartupAllAPs to fill in the rest.
  //
  if (MpServices != NULL) {
    //
    // Pass data to the wrapped Argument.
    //
    Argument.MpServices = MpServices;
    Argument.Reports    = Reports;
    Argument.CpuInfo    = CpuInfo;

    Status = MpServices->StartupAllAPs (
                           MpServices,
                           OcCpuGetMsrReportPerCore,
                           TRUE,
                           NULL,
                           5000000,
                           &Argument,
                           NULL
                           );
  }

  //
  // Update number of cores.
  //
  *EntryCount = NumberOfProcessors;

  return Reports;
}

VOID
OcCpuCorrectFlexRatio (
  IN OC_CPU_INFO  *Cpu
  )
{
  UINT64  Msr;
  UINT64  FlexRatio;

  if (  (Cpu->Vendor[0] == CPUID_VENDOR_INTEL)
     && (Cpu->CpuGeneration != OcCpuGenerationBonnell)
     && (Cpu->CpuGeneration != OcCpuGenerationSilvermont))
  {
    Msr = AsmReadMsr64 (MSR_FLEX_RATIO);
    if (Msr & FLEX_RATIO_EN) {
      FlexRatio = BitFieldRead64 (Msr, 8, 15);
      if (FlexRatio == 0) {
        //
        // Disable Flex Ratio if current value is 0.
        //
        AsmWriteMsr64 (MSR_FLEX_RATIO, Msr & ~((UINT64)FLEX_RATIO_EN));
      }
    }
  }
}

EFI_STATUS
OcCpuEnableVmx (
  VOID
  )
{
  CPUID_VERSION_INFO_ECX             RegEcx;
  MSR_IA32_FEATURE_CONTROL_REGISTER  Msr;

  AsmCpuid (1, 0, 0, &RegEcx.Uint32, 0);
  if (RegEcx.Bits.VMX == 0) {
    return EFI_UNSUPPORTED;
  }

  Msr.Uint64 = AsmReadMsr64 (MSR_IA32_FEATURE_CONTROL);
  if (Msr.Bits.Lock != 0) {
    return EFI_WRITE_PROTECTED;
  }

  //
  // Unclear if pre-existing valid bits should ever be present if register is unlocked.
  //
  Msr.Bits.Lock                = 1;
  Msr.Bits.EnableVmxOutsideSmx = 1;
  AsmWriteMsr64 (MSR_IA32_FEATURE_CONTROL, Msr.Uint64);

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
SyncTscOnCpu (
  IN  VOID  *Buffer
  )
{
  OC_CPU_TSC_SYNC  *Sync;

  Sync = Buffer;
  AsmIncrementUint32 (&Sync->CurrentCount);
  while (Sync->CurrentCount < Sync->APThreadCount) {
    //
    // Busy-wait on AP CPU cores.
    //
  }

  AsmWriteMsr64 (MSR_IA32_TIME_STAMP_COUNTER, Sync->Tsc);
}

STATIC
VOID
EFIAPI
ResetAdjustTsc (
  IN  VOID  *Buffer
  )
{
  OC_CPU_TSC_SYNC  *Sync;

  Sync = Buffer;
  AsmIncrementUint32 (&Sync->CurrentCount);
  while (Sync->CurrentCount < Sync->APThreadCount) {
    //
    // Busy-wait on AP CPU cores.
    //
  }

  AsmWriteMsr64 (MSR_IA32_TSC_ADJUST, 0);
}

EFI_STATUS
OcCpuCorrectTscSync (
  IN OC_CPU_INFO  *Cpu,
  IN UINTN        Timeout
  )
{
  EFI_STATUS                          Status;
  EFI_MP_SERVICES_PROTOCOL            *MpServices;
  FRAMEWORK_EFI_MP_SERVICES_PROTOCOL  *FrameworkMpServices;
  OC_CPU_TSC_SYNC                     Sync;
  EFI_TPL                             OldTpl;
  BOOLEAN                             InterruptState;

  if (Cpu->ThreadCount <= 1) {
    DEBUG ((DEBUG_INFO, "OCCPU: Thread count is too low for sync - %u\n", Cpu->ThreadCount));
    return EFI_UNSUPPORTED;
  }

  Status = gBS->LocateProtocol (
                  &gEfiMpServiceProtocolGuid,
                  NULL,
                  (VOID **)&MpServices
                  );

  if (EFI_ERROR (Status)) {
    MpServices = NULL;
    Status     = gBS->LocateProtocol (
                        &gFrameworkEfiMpServiceProtocolGuid,
                        NULL,
                        (VOID **)&FrameworkMpServices
                        );

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "OCCPU: Failed to find mp services - %r\n", Status));
      return Status;
    }
  }

  Sync.CurrentCount  = 0;
  Sync.APThreadCount = Cpu->ThreadCount - 1;

  OldTpl         = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  InterruptState = SaveAndDisableInterrupts ();

  if (Cpu->TscAdjust > 0) {
    if (MpServices != NULL) {
      Status = MpServices->StartupAllAPs (MpServices, ResetAdjustTsc, FALSE, NULL, Timeout, &Sync, NULL);
    } else {
      Status = FrameworkMpServices->StartupAllAPs (FrameworkMpServices, ResetAdjustTsc, FALSE, NULL, Timeout, &Sync, NULL);
    }

    AsmWriteMsr64 (MSR_IA32_TSC_ADJUST, 0);
  } else {
    Sync.Tsc = AsmReadTsc ();

    if (MpServices != NULL) {
      Status = MpServices->StartupAllAPs (MpServices, SyncTscOnCpu, FALSE, NULL, Timeout, &Sync, NULL);
    } else {
      Status = FrameworkMpServices->StartupAllAPs (FrameworkMpServices, SyncTscOnCpu, FALSE, NULL, Timeout, &Sync, NULL);
    }

    AsmWriteMsr64 (MSR_IA32_TIME_STAMP_COUNTER, Sync.Tsc);
  }

  SetInterruptState (InterruptState);
  gBS->RestoreTPL (OldTpl);

  DEBUG ((DEBUG_INFO, "OCCPU: Completed TSC sync with code - %r\n", Status));

  return Status;
}

OC_CPU_GENERATION
InternalDetectIntelProcessorGeneration (
  IN OC_CPU_INFO  *CpuInfo
  )
{
  OC_CPU_GENERATION  CpuGeneration;

  CpuGeneration = OcCpuGenerationUnknown;
  if (CpuInfo->Family == 6) {
    switch (CpuInfo->Model) {
      case CPU_MODEL_BANIAS:
      case CPU_MODEL_DOTHAN:
        CpuGeneration = OcCpuGenerationBanias;
        break;
      case CPU_MODEL_YONAH:
      case CPU_MODEL_MEROM:
        CpuGeneration = OcCpuGenerationYonahMerom;
        break;
      case CPU_MODEL_PENRYN:
        CpuGeneration = OcCpuGenerationPenryn;
        break;
      case CPU_MODEL_NEHALEM:
      case CPU_MODEL_FIELDS:
      case CPU_MODEL_DALES:
      case CPU_MODEL_NEHALEM_EX:
        CpuGeneration = OcCpuGenerationNehalem;
        break;
      case CPU_MODEL_BONNELL:
      case CPU_MODEL_BONNELL_MID:
      case CPU_MODEL_AVOTON: /* perhaps should be distinct */
        CpuGeneration = OcCpuGenerationBonnell;
        break;
      case CPU_MODEL_DALES_32NM:
      case CPU_MODEL_WESTMERE:
      case CPU_MODEL_WESTMERE_EX:
        CpuGeneration = OcCpuGenerationWestmere;
        break;
      case CPU_MODEL_SANDYBRIDGE:
      case CPU_MODEL_JAKETOWN:
        CpuGeneration = OcCpuGenerationSandyBridge;
        break;
      case CPU_MODEL_SILVERMONT:
      case CPU_MODEL_GOLDMONT:
      case CPU_MODEL_AIRMONT:
        CpuGeneration = OcCpuGenerationSilvermont;
        break;
      case CPU_MODEL_IVYBRIDGE:
      case CPU_MODEL_IVYBRIDGE_EP:
        CpuGeneration = OcCpuGenerationIvyBridge;
        break;
      case CPU_MODEL_HASWELL:
      case CPU_MODEL_HASWELL_EP:
      case CPU_MODEL_HASWELL_ULT:
      case CPU_MODEL_CRYSTALWELL:
        CpuGeneration = OcCpuGenerationHaswell;
        break;
      case CPU_MODEL_BROADWELL:
      case CPU_MODEL_BROADWELL_EP:
      case CPU_MODEL_BRYSTALWELL:
        CpuGeneration = OcCpuGenerationBroadwell;
        break;
      case CPU_MODEL_SKYLAKE:
      case CPU_MODEL_SKYLAKE_DT:
      case CPU_MODEL_SKYLAKE_W:
        CpuGeneration = OcCpuGenerationSkylake;
        break;
      case CPU_MODEL_KABYLAKE:
      case CPU_MODEL_KABYLAKE_DT:
        //
        // Kaby has 0x9 stepping, and Coffee use 0xA / 0xB stepping.
        //
        if (CpuInfo->Stepping == 9) {
          CpuGeneration = OcCpuGenerationKabyLake;
        } else {
          CpuGeneration = OcCpuGenerationCoffeeLake;
        }

        break;
      case CPU_MODEL_CANNONLAKE:
        CpuGeneration = OcCpuGenerationCannonLake;
        break;
      case CPU_MODEL_COMETLAKE_S:
      case CPU_MODEL_COMETLAKE_U:
        CpuGeneration = OcCpuGenerationCometLake;
        break;
      case CPU_MODEL_ROCKETLAKE_S:
        CpuGeneration = OcCpuGenerationRocketLake;
        break;
      case CPU_MODEL_ICELAKE_Y:
      case CPU_MODEL_ICELAKE_U:
      case CPU_MODEL_ICELAKE_SP:
        CpuGeneration = OcCpuGenerationIceLake;
        break;
      case CPU_MODEL_TIGERLAKE_U:
        CpuGeneration = OcCpuGenerationTigerLake;
        break;
      case CPU_MODEL_ALDERLAKE_S:
        CpuGeneration = OcCpuGenerationAlderLake;
        break;
      default:
        if (CpuInfo->Model < CPU_MODEL_PENRYN) {
          CpuGeneration = OcCpuGenerationPreYonah;
        } else if (CpuInfo->Model >= CPU_MODEL_SANDYBRIDGE) {
          CpuGeneration = OcCpuGenerationPostSandyBridge;
        }
    }
  } else {
    CpuGeneration = OcCpuGenerationPreYonah;
  }

  DEBUG ((
    DEBUG_VERBOSE,
    "OCCPU: Discovered CpuFamily %d CpuModel %d CpuStepping %d CpuGeneration %d\n",
    CpuInfo->Family,
    CpuInfo->Model,
    CpuInfo->Stepping,
    CpuGeneration
    ));

  return CpuGeneration;
}
