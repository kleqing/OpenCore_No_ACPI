/** @file
  Copyright (C) 2016, The HermitCrabs Lab. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#include <Uefi.h>

#include <Guid/OcVariable.h>

#include <Protocol/OcLog.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/OcDataHubLib.h>
#include <Library/OcDebugLogLib.h>
#include <Library/OcFileLib.h>
#include <Library/OcMiscLib.h>
#include <Library/OcStringLib.h>
#include <Library/OcTimerLib.h>
#include <Library/OcVariableLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "OcLogInternal.h"

STATIC
CHAR8 *
GetTiming  (
  IN OC_LOG_PROTOCOL  *This
  )
{
  OC_LOG_PRIVATE_DATA  *Private = NULL;

  UINT64  dTStartSec = 0;
  UINT64  dTStartMs  = 0;
  UINT64  dTLastSec  = 0;
  UINT64  dTLastMs   = 0;
  UINT64  CurrentTsc = 0;

  if (This == NULL) {
    return NULL;
  }

  Private = OC_LOG_PRIVATE_DATA_FROM_OC_LOG_THIS (This);

  //
  // Calibrate TSC for timings.
  //

  if (Private->TscFrequency == 0) {
    Private->TscFrequency = OcGetTSCFrequency ();

    if (Private->TscFrequency != 0) {
      CurrentTsc = AsmReadTsc ();

      Private->TscStart = CurrentTsc;
      Private->TscLast  = CurrentTsc;
    }
  }

  if (Private->TscFrequency > 0) {
    CurrentTsc = AsmReadTsc ();

    dTStartMs  = DivU64x64Remainder (MultU64x32 (CurrentTsc - Private->TscStart, 1000), Private->TscFrequency, NULL);
    dTStartSec = DivU64x64Remainder (dTStartMs, 1000, &dTStartMs);
    dTLastMs   = DivU64x64Remainder (MultU64x32 (CurrentTsc - Private->TscLast, 1000), Private->TscFrequency, NULL);
    dTLastSec  = DivU64x64Remainder (dTLastMs, 1000, &dTLastMs);

    Private->TscLast = CurrentTsc;
  }

  AsciiSPrint (
    Private->TimingTxt,
    OC_LOG_TIMING_BUFFER_SIZE,
    "%02d:%03d %02d:%03d ",
    dTStartSec,
    dTStartMs,
    dTLastSec,
    dTLastMs
    );

  return Private->TimingTxt;
}

STATIC
CHAR16 *
GetLogPath (
  IN CONST CHAR16  *LogPrefixPath
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Date;
  CHAR16      *LogPath;
  UINTN       Size;

  if (LogPrefixPath == NULL) {
    return NULL;
  }

  Status = gRT->GetTime (&Date, NULL);
  if (EFI_ERROR (Status)) {
    ZeroMem (&Date, sizeof (Date));
  }

  Size = StrSize (LogPrefixPath) + L_STR_SIZE (L"-0000-00-00-000000.txt");

  LogPath = AllocatePool (Size);
  if (LogPath == NULL) {
    return NULL;
  }

  UnicodeSPrint (
    LogPath,
    Size,
    L"%s-%04u-%02u-%02u-%02u%02u%02u.txt",
    LogPrefixPath,
    (UINT32)Date.Year,
    (UINT32)Date.Month,
    (UINT32)Date.Day,
    (UINT32)Date.Hour,
    (UINT32)Date.Minute,
    (UINT32)Date.Second
    );

  return LogPath;
}

STATIC
EFI_STATUS
GetLogPrefix (
  IN   CONST CHAR8  *FormatString,
  OUT  CHAR8        *Prefix
  )
{
  UINTN  MaxLength;
  UINTN  Index;
  CHAR8  Curr;

  ASSERT (FormatString != NULL);
  ASSERT (Prefix != NULL);

  //
  // If FormatString just starts with colon, it must be illegal.
  //
  if (*FormatString == ':') {
    return EFI_NOT_FOUND;
  }

  MaxLength = MIN (AsciiStrLen (FormatString), OC_LOG_PREFIX_CHAR_MAX);
  for (Index = 1; Index < MaxLength; ++Index) {
    Curr = FormatString[Index];

    //
    // Match the first occurrence of colon.
    //
    if (Curr == ':') {
      break;
    }

    //
    // Except for colon, a valid prefix must be either 0-9, or uppercase letter.
    //
    if (!(IsAsciiNumber (Curr) || ((Curr >= 'A') && (Curr <= 'Z')))) {
      return EFI_NOT_FOUND;
    }
  }

  //
  // If Index went through the end, then ':' was not found.
  //
  if (Index == MaxLength) {
    return EFI_NOT_FOUND;
  }

  CopyMem (Prefix, FormatString, Index);
  Prefix[Index] = '\0';
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
IsPrefixFiltered (
  IN   CONST CHAR8          *FormatString,
  IN   CONST OC_FLEX_ARRAY  *FlexFilters    OPTIONAL,
  IN   BOOLEAN              BlacklistFiltering
  )
{
  UINTN       Index;
  CHAR8       Prefix[OC_LOG_PREFIX_CHAR_MAX + 1];
  EFI_STATUS  Status;
  CHAR8       **Value;

  ASSERT (FormatString != NULL);

  //
  // Do not filter without filters, of course.
  //
  if (FlexFilters == NULL) {
    return FALSE;
  }

  Status = GetLogPrefix (FormatString, Prefix);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  for (Index = 0; Index < FlexFilters->Count; ++Index) {
    Value = (CHAR8 **)OcFlexArrayItemAt (FlexFilters, Index);
    ASSERT (Value != NULL);

    if (AsciiStrCmp (Prefix, *Value) == 0) {
      //
      // Upon matching, return TRUE (i.e. not to print logs)
      // if blacklisted.
      //
      return BlacklistFiltering;
    }
  }

  return FALSE;
}

STATIC
EFI_STATUS
InternalLogAddEntry (
  IN OC_LOG_PRIVATE_DATA  *Private,
  IN OC_LOG_PROTOCOL      *OcLog,
  IN UINTN                ErrorLevel,
  IN CONST CHAR8          *FormatString,
  IN VA_LIST              Marker
  )
{
  EFI_STATUS                  Status;
  UINT32                      Attributes;
  UINT32                      TimingLength;
  UINT32                      LineLength;
  APPLE_PLATFORM_DATA_RECORD  *Entry;
  UINT32                      KeySize;
  UINT32                      DataSize;
  UINT32                      TotalSize;
  UINTN                       WriteSize;
  UINTN                       WrittenSize;

  AsciiVSPrint (
    Private->LineBuffer,
    sizeof (Private->LineBuffer),
    FormatString,
    Marker
    );

  //
  // Add Entry.
  //

  Status = EFI_SUCCESS;

  if (*Private->LineBuffer != '\0') {
    GetTiming (OcLog);

    //
    // Send the string to the console output device.
    //
    if (((OcLog->Options & OC_LOG_CONSOLE) != 0) && ((OcLog->DisplayLevel & ErrorLevel) != 0)) {
      UnicodeSPrint (
        Private->UnicodeLineBuffer,
        sizeof (Private->UnicodeLineBuffer),
        L"%a",
        Private->LineBuffer
        );
      gST->ConOut->OutputString (gST->ConOut, Private->UnicodeLineBuffer);

      if (OcLog->DisplayDelay > 0) {
        gBS->Stall (OcLog->DisplayDelay);
      }
    }

    TimingLength = (UINT32)AsciiStrLen (Private->TimingTxt);
    LineLength   = (UINT32)AsciiStrLen (Private->LineBuffer);

    //
    // Write to serial port.
    //
    if ((OcLog->Options & OC_LOG_SERIAL) != 0) {
      //
      // No return value check - SerialPortWrite either stalls or falsely return all bytes written if no serial available.
      //
      SerialPortWrite ((UINT8 *)Private->TimingTxt, TimingLength);
      SerialPortWrite ((UINT8 *)Private->LineBuffer, LineLength);
    }

    //
    // Write to DataHub.
    //
    if ((OcLog->Options & OC_LOG_DATA_HUB) != 0) {
      if (Private->DataHub == NULL) {
        gBS->LocateProtocol (
               &gEfiDataHubProtocolGuid,
               NULL,
               (VOID **)&Private->DataHub
               );
      }

      if (Private->DataHub != NULL) {
        KeySize   = (L_STR_LEN (OC_LOG_VARIABLE_NAME) + 6) * sizeof (CHAR16);
        DataSize  = TimingLength + LineLength + 1;
        TotalSize = KeySize + DataSize + sizeof (*Entry);

        Entry = AllocatePool (TotalSize);

        if (Entry != NULL) {
          ZeroMem (Entry, sizeof (*Entry));
          Entry->KeySize   = KeySize;
          Entry->ValueSize = DataSize;

          UnicodeSPrint (
            (CHAR16 *)&Entry->Data[0],
            Entry->KeySize,
            L"%s%05u",
            OC_LOG_VARIABLE_NAME,
            Private->LogCounter++
            );

          CopyMem (
            &Entry->Data[Entry->KeySize],
            Private->TimingTxt,
            TimingLength
            );

          CopyMem (
            &Entry->Data[Entry->KeySize + TimingLength],
            Private->LineBuffer,
            LineLength + 1
            );

          Private->DataHub->LogData (
                              Private->DataHub,
                              &gEfiMiscSubClassGuid,
                              &gApplePlatformProducerNameGuid,
                              EFI_DATA_RECORD_CLASS_DATA,
                              Entry,
                              TotalSize
                              );

          FreePool (Entry);
        }
      }
    }

    //
    // Write to internal buffer.
    //
    Status = AsciiStrCatS (Private->AsciiBuffer, Private->AsciiBufferSize, Private->TimingTxt);
    if (!EFI_ERROR (Status)) {
      Private->AsciiBufferWrittenOffset += AsciiStrLen (Private->TimingTxt);
      Status                             = AsciiStrCatS (Private->AsciiBuffer, Private->AsciiBufferSize, Private->LineBuffer);
      if (!EFI_ERROR (Status)) {
        Private->AsciiBufferWrittenOffset += AsciiStrLen (Private->LineBuffer);
      }
    }

    //
    // Write to a file.
    //
    if (((OcLog->Options & OC_LOG_FILE) != 0) && (OcLog->FileSystem != NULL)) {
      //
      // Log lines may arrive when CurrentTpl > TPL_CALLBACK, we must batch them
      // and emit them when we can, in both log methods.
      //
      if (EfiGetCurrentTpl () <= TPL_CALLBACK) {
        if (OcLog->UnsafeLogFile != NULL) {
          //
          // For non-broken FAT32 driver this is fine. For driver with broken write
          // support (e.g. Aptio IV) this can result in corrupt file or unusable fs.
          //
          ASSERT (Private->AsciiBufferWrittenOffset >= Private->AsciiBufferFlushedOffset);
          WriteSize   = Private->AsciiBufferWrittenOffset - Private->AsciiBufferFlushedOffset;
          WrittenSize = WriteSize;
          OcLog->UnsafeLogFile->Write (OcLog->UnsafeLogFile, &WrittenSize, &Private->AsciiBuffer[Private->AsciiBufferFlushedOffset]);
          OcLog->UnsafeLogFile->Flush (OcLog->UnsafeLogFile);
          Private->AsciiBufferFlushedOffset += WrittenSize;
          if (WriteSize != WrittenSize) {
            DEBUG ((
              DEBUG_VERBOSE,
              "OCL: Log write truncated %u to %u\n",
              WriteSize,
              WrittenSize
              ));
          }
        } else {
          //
          // Always overwriting file completely is most reliable.
          // It is slow, but fixed size write is more reliable with broken FAT32 driver.
          //
          OcSetFileData (
            OcLog->FileSystem,
            OcLog->FilePath,
            Private->AsciiBuffer,
            (UINT32)Private->AsciiBufferSize
            );
        }
      }
    }

    //
    // Write to a variable.
    //
    if ((ErrorLevel != DEBUG_BULK_INFO) && ((OcLog->Options & (OC_LOG_VARIABLE | OC_LOG_NONVOLATILE)) != 0)) {
      //
      // Do not log timing information to NVRAM, it is already large.
      // This check is here, because Microsoft is retarded and asserts.
      //
      if (Private->NvramBufferSize - AsciiStrSize (Private->NvramBuffer) >= AsciiStrLen (Private->LineBuffer)) {
        Status = AsciiStrCatS (Private->NvramBuffer, Private->NvramBufferSize, Private->LineBuffer);
      } else {
        Status = EFI_BUFFER_TOO_SMALL;
      }

      if (!EFI_ERROR (Status)) {
        Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
        if ((OcLog->Options & OC_LOG_NONVOLATILE) != 0) {
          Attributes |= EFI_VARIABLE_NON_VOLATILE;
        }

        //
        // Do not use OcSetSystemVariable() as persistence is configured by the
        // user.
        //
        Status = gRT->SetVariable (
                        OC_LOG_VARIABLE_NAME,
                        &gOcVendorVariableGuid,
                        Attributes,
                        AsciiStrLen (Private->NvramBuffer),
                        Private->NvramBuffer
                        );

        if (EFI_ERROR (Status)) {
          //
          // On APTIO V this may not even get printed. Regardless of volatile or not
          // it will firstly start discarding NVRAM data silently, and then will borks
          // NVRAM support completely till reboot. Let's stop on first error at least.
          //
          gST->ConOut->OutputString (gST->ConOut, L"NVRAM is full, cannot log!\r\n");
          gBS->Stall (SECONDS_TO_MICROSECONDS (1));
          OcLog->Options &= ~(OC_LOG_VARIABLE | OC_LOG_NONVOLATILE);
        }
      } else {
        gST->ConOut->OutputString (gST->ConOut, L"NVRAM log size exceeded, cannot log!\r\n");
        gBS->Stall (SECONDS_TO_MICROSECONDS (1));
        OcLog->Options &= ~(OC_LOG_VARIABLE | OC_LOG_NONVOLATILE);
      }
    }
  }

  return Status;
}

EFI_STATUS
EFIAPI
OcLogAddEntry (
  IN OC_LOG_PROTOCOL  *OcLog,
  IN UINTN            ErrorLevel,
  IN CONST CHAR8      *FormatString,
  IN VA_LIST          Marker
  )
{
  EFI_STATUS           Status;
  OC_LOG_PRIVATE_DATA  *Private;
  BOOLEAN              IsFiltered;

  ASSERT (OcLog != NULL);
  ASSERT (FormatString != NULL);

  Private = OC_LOG_PRIVATE_DATA_FROM_OC_LOG_THIS (OcLog);

  if ((OcLog->Options & OC_LOG_ENABLE) == 0) {
    //
    // Silently ignore when disabled.
    //
    return EFI_SUCCESS;
  }

  //
  // Filter log.
  //
  Status     = EFI_SUCCESS;
  IsFiltered = IsPrefixFiltered (FormatString, Private->FlexFilters, Private->BlacklistFiltering);
  if (!IsFiltered) {
    Status = InternalLogAddEntry (Private, OcLog, ErrorLevel, FormatString, Marker);
  }

  if (  ((ErrorLevel & OcLog->HaltLevel) != 0)
     && (AsciiStrnCmp (FormatString, "\nASSERT_RETURN_ERROR", L_STR_LEN ("\nASSERT_RETURN_ERROR")) != 0)
     && (AsciiStrnCmp (FormatString, "\nASSERT_EFI_ERROR", L_STR_LEN ("\nASSERT_EFI_ERROR")) != 0))
  {
    gST->ConOut->OutputString (gST->ConOut, L"Halting on critical error\r\n");
    gBS->Stall (SECONDS_TO_MICROSECONDS (1));
    CpuDeadLoop ();
  }

  return Status;
}

EFI_STATUS
EFIAPI
OcLogGetLog  (
  IN  OC_LOG_PROTOCOL  *This,
  OUT CHAR8            **OcLogBuffer
  )
{
  EFI_STATUS  Status;

  OC_LOG_PRIVATE_DATA  *Private;

  Status = EFI_INVALID_PARAMETER;

  if (OcLogBuffer != NULL) {
    Private      = OC_LOG_PRIVATE_DATA_FROM_OC_LOG_THIS (This);
    *OcLogBuffer = Private->AsciiBuffer;

    Status = EFI_SUCCESS;
  }

  return Status;
}

EFI_STATUS
EFIAPI
OcLogSaveLog (
  IN OC_LOG_PROTOCOL           *This,
  IN UINT32                    NonVolatile OPTIONAL,
  IN EFI_DEVICE_PATH_PROTOCOL  *FilePath OPTIONAL
  )
{
  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
OcLogResetTimers (
  IN OC_LOG_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

OC_LOG_PROTOCOL *
InternalGetOcLog (
  VOID
  )
{
  EFI_STATUS  Status;

  STATIC OC_LOG_PROTOCOL  *mInternalOcLog = NULL;

  if (mInternalOcLog == NULL) {
    Status = gBS->LocateProtocol (
                    &gOcLogProtocolGuid,
                    NULL,
                    (VOID **)&mInternalOcLog
                    );

    if (EFI_ERROR (Status) || (mInternalOcLog->Revision != OC_LOG_REVISION)) {
      mInternalOcLog = NULL;
    }
  }

  return mInternalOcLog;
}

EFI_STATUS
OcConfigureLogProtocol (
  IN OC_LOG_OPTIONS                   Options,
  IN CONST CHAR8                      *LogModules,
  IN UINT32                           DisplayDelay,
  IN UINTN                            DisplayLevel,
  IN UINTN                            HaltLevel,
  IN CONST CHAR16                     *LogPrefixPath  OPTIONAL,
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *LogFileSystem  OPTIONAL
  )
{
  EFI_STATUS  Status;

  OC_LOG_PROTOCOL      *OcLog;
  OC_LOG_PRIVATE_DATA  *Private;
  EFI_HANDLE           Handle;
  EFI_FILE_PROTOCOL    *LogRoot;
  CHAR16               *LogPath;
  EFI_FILE_PROTOCOL    *UnsafeLogFile;

  ASSERT (LogModules != NULL);

  if ((Options & (OC_LOG_FILE | OC_LOG_ENABLE)) == (OC_LOG_FILE | OC_LOG_ENABLE)) {
    LogRoot       = NULL;
    LogPath       = GetLogPath (LogPrefixPath);
    UnsafeLogFile = NULL;

    if (LogPath != NULL) {
      if (LogFileSystem != NULL) {
        Status = LogFileSystem->OpenVolume (LogFileSystem, &LogRoot);
        if (EFI_ERROR (Status)) {
          LogRoot = NULL;
        } else if (!OcIsWritableFileSystem (LogRoot)) {
          LogRoot->Close (LogRoot);
          LogRoot = NULL;
        }
      }

      if (LogRoot == NULL) {
        Status = OcFindWritableFileSystem (&LogRoot);
        if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_ERROR, "OCL: There is no place to write log file to - %r\n", Status));
          LogRoot = NULL;
        }
      }

      if ((LogRoot != NULL) && ((Options & OC_LOG_UNSAFE) != 0)) {
        Status = OcSafeFileOpen (
                   LogRoot,
                   &UnsafeLogFile,
                   LogPath,
                   EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                   0
                   );
        if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_ERROR, "OCL: Failure opening log file - %r\n", Status));
          UnsafeLogFile = NULL;
          LogRoot->Close (LogRoot);
          LogRoot = NULL;
        }
      }

      if (LogRoot == NULL) {
        FreePool (LogPath);
        LogPath = NULL;
      }
    }
  } else {
    LogRoot       = NULL;
    LogPath       = NULL;
    UnsafeLogFile = NULL;
  }

  //
  // Check if protocol already exists.
  //

  OcLog = InternalGetOcLog ();

  if (OcLog != NULL) {
    //
    // Set desired options in existing protocol.
    //

    if (OcLog->FileSystem != NULL) {
      OcLog->FileSystem->Close (OcLog->FileSystem);
    }

    if (OcLog->UnsafeLogFile != NULL) {
      OcLog->UnsafeLogFile->Close (OcLog->UnsafeLogFile);
    }

    if (OcLog->FilePath != NULL) {
      FreePool (OcLog->FilePath);
    }

    OcLog->Options       = Options;
    OcLog->DisplayDelay  = DisplayDelay;
    OcLog->DisplayLevel  = DisplayLevel;
    OcLog->HaltLevel     = HaltLevel;
    OcLog->FileSystem    = LogRoot;
    OcLog->FilePath      = LogPath;
    OcLog->UnsafeLogFile = UnsafeLogFile;

    Status = EFI_SUCCESS;
  } else {
    Private = AllocateZeroPool (sizeof (*Private));
    Status  = EFI_OUT_OF_RESOURCES;

    if (Private != NULL) {
      Private->Signature           = OC_LOG_PRIVATE_DATA_SIGNATURE;
      Private->AsciiBufferSize     = OC_LOG_BUFFER_SIZE;
      Private->NvramBufferSize     = OC_LOG_NVRAM_BUFFER_SIZE;
      Private->OcLog.Revision      = OC_LOG_REVISION;
      Private->OcLog.AddEntry      = OcLogAddEntry;
      Private->OcLog.GetLog        = OcLogGetLog;
      Private->OcLog.SaveLog       = OcLogSaveLog;
      Private->OcLog.ResetTimers   = OcLogResetTimers;
      Private->OcLog.Options       = Options;
      Private->OcLog.DisplayDelay  = DisplayDelay;
      Private->OcLog.DisplayLevel  = DisplayLevel;
      Private->OcLog.HaltLevel     = HaltLevel;
      Private->OcLog.FileSystem    = LogRoot;
      Private->OcLog.FilePath      = LogPath;
      Private->OcLog.UnsafeLogFile = UnsafeLogFile;

      //
      // Write filters into Private.
      //
      Private->FlexFilters        = NULL;
      Private->BlacklistFiltering = FALSE;
      if ((*LogModules != '*') && (*LogModules != '\0')) {
        //
        // Default to positive filtering without symbol.
        //
        if (*LogModules == '+') {
          ++LogModules;
        } else if (*LogModules == '-') {
          Private->BlacklistFiltering = TRUE;
          ++LogModules;
        }

        Private->FlexFilters = OcStringSplit (LogModules, L',', OcStringFormatAscii);
      }

      Handle = NULL;
      Status = gBS->InstallProtocolInterface (
                      &Handle,
                      &gOcLogProtocolGuid,
                      EFI_NATIVE_INTERFACE,
                      &Private->OcLog
                      );

      if (!EFI_ERROR (Status)) {
        OcLog = &Private->OcLog;
      } else {
        FreePool (Private);
      }
    }
  }

  if (LogRoot != NULL) {
    if (!EFI_ERROR (Status)) {
      if (  ((Options & OC_LOG_UNSAFE) == 0)
         && (OC_LOG_PRIVATE_DATA_FROM_OC_LOG_THIS (OcLog)->AsciiBufferSize > 0)
            )
      {
        OcSetFileData (
          LogRoot,
          LogPath,
          OC_LOG_PRIVATE_DATA_FROM_OC_LOG_THIS (OcLog)->AsciiBuffer,
          (UINT32)OC_LOG_PRIVATE_DATA_FROM_OC_LOG_THIS (OcLog)->AsciiBufferSize
          );
      }
    } else {
      if (UnsafeLogFile != NULL) {
        UnsafeLogFile->Close (UnsafeLogFile);
      }

      LogRoot->Close (LogRoot);
      FreePool (LogPath);
    }
  }

  return Status;
}
