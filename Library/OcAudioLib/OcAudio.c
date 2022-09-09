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

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OcAudioLib.h>
#include <Library/OcDevicePathLib.h>
#include <Library/OcMiscLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/AppleHda.h>
#include <Protocol/AppleBeepGen.h>
#include <Protocol/AppleVoiceOver.h>
#include <Protocol/HdaIo.h>

#include "OcAudioInternal.h"

STATIC
EFI_DEVICE_PATH_PROTOCOL *
OcAudioGetCodecDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *ControllerDevicePath,
  IN UINT8                     CodecAddress
  )
{
  EFI_HDA_IO_DEVICE_PATH  CodecDevicePathNode;

  CodecDevicePathNode.Header.Type    = MESSAGING_DEVICE_PATH;
  CodecDevicePathNode.Header.SubType = MSG_VENDOR_DP;
  SetDevicePathNodeLength (&CodecDevicePathNode, sizeof (CodecDevicePathNode));
  CopyGuid (&CodecDevicePathNode.Guid, &gEfiHdaIoDevicePathGuid);
  CodecDevicePathNode.Address = CodecAddress;

  return AppendDevicePathNode (
           ControllerDevicePath,
           (EFI_DEVICE_PATH_PROTOCOL *)&CodecDevicePathNode
           );
}

STATIC
EFI_STATUS
AudioIoProtocolConfirmRevision (
  IN CONST EFI_AUDIO_IO_PROTOCOL  *AudioIo
  )
{
  if (AudioIo->Revision == EFI_AUDIO_IO_PROTOCOL_REVISION) {
    return EFI_SUCCESS;
  }

  DEBUG ((
    DEBUG_WARN,
    "OCAU: Incorrect audio I/O protocol revision %u != %u\n",
    AudioIo->Revision,
    EFI_AUDIO_IO_PROTOCOL_REVISION
    ));

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
InternalMatchCodecDevicePath (
  IN OUT OC_AUDIO_PROTOCOL_PRIVATE  *Private,
  IN     EFI_DEVICE_PATH_PROTOCOL   *DevicePath,
  IN     EFI_HANDLE                 *AudioIoHandles,
  IN     UINTN                      AudioIoHandleCount
  )
{
  EFI_STATUS                  Status;
  UINTN                       Index;
  CHAR16                      *DevicePathText;
  EFI_DEVICE_PATH_PROTOCOL    *CodecDevicePath;
  EFI_AUDIO_IO_PROTOCOL_PORT  *OutputPorts;
  UINTN                       OutputPortsCount;

  DEBUG_CODE_BEGIN ();
  DevicePathText = ConvertDevicePathToText (DevicePath, FALSE, FALSE);
  DEBUG ((
    DEBUG_INFO,
    "OCAU: Matching %s...\n",
    DevicePathText != NULL ? DevicePathText : L"<invalid>"
    ));
  if (DevicePathText != NULL) {
    FreePool (DevicePathText);
  }

  DEBUG_CODE_END ();

  for (Index = 0; Index < AudioIoHandleCount; ++Index) {
    Status = gBS->HandleProtocol (
                    AudioIoHandles[Index],
                    &gEfiDevicePathProtocolGuid,
                    (VOID **)&CodecDevicePath
                    );

    DEBUG_CODE_BEGIN ();
    DevicePathText = NULL;
    if (!EFI_ERROR (Status)) {
      DevicePathText = ConvertDevicePathToText (CodecDevicePath, FALSE, FALSE);
    }

    OutputPortsCount = 0;
    Status           = gBS->HandleProtocol (
                              AudioIoHandles[Index],
                              &gEfiAudioIoProtocolGuid,
                              (VOID **)&Private->AudioIo
                              );
    if (!EFI_ERROR (Status)) {
      Status = AudioIoProtocolConfirmRevision (Private->AudioIo);
    }

    if (!EFI_ERROR (Status)) {
      Status = Private->AudioIo->GetOutputs (
                                   Private->AudioIo,
                                   &OutputPorts,
                                   &OutputPortsCount
                                   );
      if (!EFI_ERROR (Status)) {
        FreePool (OutputPorts);
      }
    }

    DEBUG ((
      DEBUG_INFO,
      "OCAU: %u/%u %s (%u outputs) - %r\n",
      (UINT32)(Index + 1),
      (UINT32)(AudioIoHandleCount),
      DevicePathText != NULL ? DevicePathText : L"<invalid>",
      (UINT32)OutputPortsCount,
      Status
      ));

    if (DevicePathText != NULL) {
      FreePool (DevicePathText);
    }

    DEBUG_CODE_END ();

    if (IsDevicePathEqual (DevicePath, CodecDevicePath)) {
      Status = gBS->HandleProtocol (
                      AudioIoHandles[Index],
                      &gEfiAudioIoProtocolGuid,
                      (VOID **)&Private->AudioIo
                      );
      if (!EFI_ERROR (Status)) {
        Status = AudioIoProtocolConfirmRevision (Private->AudioIo);
      }

      return Status;
    }
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
InternalOcAudioSetDefaultGain (
  IN OUT OC_AUDIO_PROTOCOL  *This,
  IN     INT8               Gain
  )
{
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  Private->Gain = Gain;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
InternalOcAudioConnect (
  IN OUT OC_AUDIO_PROTOCOL         *This,
  IN     EFI_DEVICE_PATH_PROTOCOL  *DevicePath      OPTIONAL,
  IN     UINT8                     CodecAddress     OPTIONAL,
  IN     UINT64                    OutputIndexMask
  )
{
  EFI_STATUS                 Status;
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;
  EFI_HANDLE                 *AudioIoHandles;
  UINTN                      AudioIoHandleCount;
  EFI_DEVICE_PATH_PROTOCOL   *TmpDevicePath;

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  Private->OutputIndexMask = OutputIndexMask;

  if (DevicePath == NULL) {
    Status = gBS->LocateProtocol (
                    &gEfiAudioIoProtocolGuid,
                    NULL,
                    (VOID **)&Private->AudioIo
                    );
    if (!EFI_ERROR (Status)) {
      Status = AudioIoProtocolConfirmRevision (Private->AudioIo);
    }
  } else {
    Status = gBS->LocateHandleBuffer (
                    ByProtocol,
                    &gEfiAudioIoProtocolGuid,
                    NULL,
                    &AudioIoHandleCount,
                    &AudioIoHandles
                    );

    if (!EFI_ERROR (Status)) {
      DevicePath = OcAudioGetCodecDevicePath (DevicePath, CodecAddress);
      if (DevicePath == NULL) {
        DEBUG ((DEBUG_INFO, "OCAU: Cannot get full device path\n"));
        FreePool (AudioIoHandles);
        return EFI_INVALID_PARAMETER;
      }

      Status = InternalMatchCodecDevicePath (
                 Private,
                 DevicePath,
                 AudioIoHandles,
                 AudioIoHandleCount
                 );

      if (EFI_ERROR (Status)) {
        //
        // WARN: DevicePath must be allocated from pool as it may be reallocated.
        //
        if (OcFixAppleBootDevicePath (&DevicePath, &TmpDevicePath) > 0) {
          DEBUG ((DEBUG_INFO, "OCAU: Retrying with fixed device path\n"));
          Status = InternalMatchCodecDevicePath (
                     Private,
                     DevicePath,
                     AudioIoHandles,
                     AudioIoHandleCount
                     );
        }
      }

      FreePool (DevicePath);
      FreePool (AudioIoHandles);
    } else {
      DEBUG ((DEBUG_INFO, "OCAU: No AudioIo instances - %r\n", Status));
    }
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCAU: Cannot find specified audio device - %r\n", Status));
    Private->AudioIo = NULL;
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
InternalOcAudioSetProvider (
  IN OUT OC_AUDIO_PROTOCOL          *This,
  IN     OC_AUDIO_PROVIDER_ACQUIRE  Acquire,
  IN     OC_AUDIO_PROVIDER_RELEASE  Release  OPTIONAL,
  IN     VOID                       *Context
  )
{
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;

  if (Acquire == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  Private->ProviderAcquire = Acquire;
  Private->ProviderRelease = Release;
  Private->ProviderContext = Context;

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
InernalOcAudioPlayFileDone (
  IN EFI_AUDIO_IO_PROTOCOL  *AudioIo,
  IN VOID                   *Context
  )
{
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;

  Private = Context;

  DEBUG ((DEBUG_VERBOSE, "OCAU: PlayFileDone signaling for completion\n"));

  //
  // The event callback is guaranteed to be called with TPL_NOTIFY,
  // therefore we are guaranteed to have audio buffer set here.
  //
  ASSERT (Private->CurrentBuffer != NULL);

  if (Private->ProviderRelease != NULL) {
    Private->ProviderRelease (Private->ProviderContext, Private->CurrentBuffer);
  }

  Private->CurrentBuffer = NULL;

  gBS->SignalEvent (Private->PlaybackEvent);
}

EFI_STATUS
EFIAPI
InternalOcAudioRawGainToDecibels (
  IN OUT OC_AUDIO_PROTOCOL  *This,
  IN     UINT8              GainParam,
  OUT INT8                  *Gain
  )
{
  EFI_STATUS                 Status;
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  if (Private->AudioIo == NULL) {
    DEBUG ((DEBUG_INFO, "OCAU: RawGainToDecibels has no AudioIo\n"));
    return EFI_ABORTED;
  }

  Status = Private->AudioIo->RawGainToDecibels (
                               Private->AudioIo,
                               Private->OutputIndexMask,
                               GainParam,
                               Gain
                               );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCAU: RawGainToDecibels conversion failure - %r\n", Status));
  }

  return Status;
}

EFI_STATUS
EFIAPI
InternalOcAudioPlayFile (
  IN OUT OC_AUDIO_PROTOCOL  *This,
  IN     CONST CHAR8        *BasePath,
  IN     CONST CHAR8        *BaseType,
  IN     BOOLEAN            Localised,
  IN     INT8               Gain  OPTIONAL,
  IN     BOOLEAN            UseGain,
  IN     BOOLEAN            Wait
  )
{
  EFI_STATUS                  Status;
  OC_AUDIO_PROTOCOL_PRIVATE   *Private;
  UINT8                       *RawBuffer;
  UINT32                      RawBufferSize;
  EFI_AUDIO_IO_PROTOCOL_FREQ  Frequency;
  EFI_AUDIO_IO_PROTOCOL_BITS  Bits;
  UINT8                       Channels;
  EFI_TPL                     OldTpl;

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  if ((Private->AudioIo == NULL) || (Private->ProviderAcquire == NULL)) {
    DEBUG ((DEBUG_INFO, "OCAU: PlayFile has no AudioIo or provider is unconfigured\n"));
    return EFI_ABORTED;
  }

  Status = Private->ProviderAcquire (
                      Private->ProviderContext,
                      BasePath,
                      BaseType,
                      Localised,
                      Private->Language,
                      &RawBuffer,
                      &RawBufferSize,
                      &Frequency,
                      &Bits,
                      &Channels
                      );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCAU: PlayFile has no file %a for type %a lang %u - %r\n", BasePath, BaseType, Private->Language, Status));
    return EFI_NOT_FOUND;
  }

  DEBUG ((
    DEBUG_INFO,
    "OCAU: File %a for type %a lang %u is %d %d %d (%u) - %r\n",
    BasePath,
    BaseType,
    Private->Language,
    Frequency,
    Bits,
    Channels,
    (UINT32)RawBufferSize,
    Status
    ));

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCAU: PlayFile has invalid file %a for type %a lang %u - %r\n", BasePath, BaseType, Private->Language, Status));
    if (Private->ProviderRelease != NULL) {
      Private->ProviderRelease (Private->ProviderContext, RawBuffer);
    }

    return EFI_NOT_FOUND;
  }

  This->StopPlayback (This, Wait);

  OldTpl                 = gBS->RaiseTPL (TPL_NOTIFY);
  Private->CurrentBuffer = RawBuffer;

  Status = Private->AudioIo->SetupPlayback (
                               Private->AudioIo,
                               Private->OutputIndexMask,
                               UseGain ? Gain : Private->Gain,
                               Frequency,
                               Bits,
                               Channels,
                               Private->PlaybackDelay
                               );
  if (!EFI_ERROR (Status)) {
    Status = Private->AudioIo->StartPlaybackAsync (
                                 Private->AudioIo,
                                 RawBuffer,
                                 RawBufferSize,
                                 0,
                                 InernalOcAudioPlayFileDone,
                                 Private
                                 );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "OCAU: PlayFile playback failure - %r\n", Status));
    }
  } else {
    DEBUG ((DEBUG_INFO, "OCAU: PlayFile playback setup failure - %r\n", Status));
  }

  if (EFI_ERROR (Status)) {
    if (Private->ProviderRelease != NULL) {
      Private->ProviderRelease (Private->ProviderContext, Private->CurrentBuffer);
    }

    Private->CurrentBuffer = NULL;
  }

  gBS->RestoreTPL (OldTpl);
  return Status;
}

EFI_STATUS
EFIAPI
InternalOcAudioStopPlayback (
  IN OUT OC_AUDIO_PROTOCOL  *This,
  IN     BOOLEAN            Wait
  )
{
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;
  EFI_TPL                    OldTpl;
  UINTN                      Index;
  EFI_STATUS                 Status;
  BOOLEAN                    CheckEvent;

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  //
  // Note, this function cannot call prints, as it may be called from within
  // ExitBootServices handler.
  //

  DEBUG ((DEBUG_VERBOSE, "OCAU: StopPlayback %d %p\n", Wait, Private->CurrentBuffer != NULL));

  //
  // Ensure that we never have the events signaled.
  //
  CheckEvent = TRUE;

  if (Wait) {
    //
    // CurrentBuffer is set when asynchronous audio data is playing.
    // Try to wait for asynchronous audio playback for complete.
    //
    if (Private->CurrentBuffer != NULL) {
      Status = gBS->WaitForEvent (1, &Private->PlaybackEvent, &Index);
      DEBUG ((DEBUG_VERBOSE, "OCAU: StopPlayback wait - %r\n", Status));
      //
      // This can fail in the following cases when current TPL is not TPL_APPLICATION.
      // boot.efi does it from TPL_NOTIFY for password clicks in FV2 UI.
      //
      if (!EFI_ERROR (Status)) {
        //
        // If our wait was a success, we must have freed the buffer due to callback
        // execution (InernalOcAudioPlayFileDone).
        //
        CheckEvent = FALSE;
        ASSERT (Private->CurrentBuffer == NULL);
      }
    }
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  if (Private->CurrentBuffer != NULL) {
    //
    // The audio is still playing. Stop playback now.
    //
    Private->AudioIo->StopPlayback (
                        Private->AudioIo
                        );

    //
    // Calling StopPlayback ignores the registered callback, free file here.
    //
    if (Private->ProviderRelease != NULL) {
      Private->ProviderRelease (Private->ProviderContext, Private->CurrentBuffer);
    }

    Private->CurrentBuffer = NULL;
  }

  if (CheckEvent) {
    //
    // 1. It is possible that the audio completed before we waited, and thus
    //    Private->CurrentBuffer was NULL at the time we checked it.
    // 2. It is possible that we WaitForEvent failed due to wrong TPL.
    // 3. It is possible that we were called with Wait = FALSE, and in this
    //    case we still need to ensure that the event is reset for next playback.
    // CheckEvent may fail if neither of these is true, and this is expected.
    // We can call CheckEvent with TPL_APPLICATION, as a call to StopPlayback
    // in TPL_NOTIFY guarantees no callbacks.
    //
    Status = gBS->CheckEvent (Private->PlaybackEvent);
    DEBUG ((DEBUG_VERBOSE, "OCAU: StopPlayback check - %r\n", Status));
  }

  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;
}

UINTN
EFIAPI
InternalOcAudioSetDelay (
  IN OUT OC_AUDIO_PROTOCOL  *This,
  IN     UINTN              Delay
  )
{
  OC_AUDIO_PROTOCOL_PRIVATE  *Private;
  UINTN                      PreviousDelay;

  Private = OC_AUDIO_PROTOCOL_PRIVATE_FROM_OC_AUDIO (This);

  PreviousDelay          = Private->PlaybackDelay;
  Private->PlaybackDelay = Delay;

  return PreviousDelay;
}
