/*
 * File: AudioDxe.h
 *
 * Copyright (c) 2018 John Davis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef EFI_AUDIODXE_H
#define EFI_AUDIODXE_H

//
// Common UEFI includes and library classes.
//
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

#include <IndustryStandard/HdaVerbs.h>

//
// Proctols that are consumed/produced.
//
#include <Protocol/AudioDecode.h>
#include <Protocol/AudioIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathUtilities.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/HdaIo.h>
#include <Protocol/HdaCodecInfo.h>
#include <Protocol/HdaControllerInfo.h>

// Driver version
#define AUDIODXE_VERSION      0xD
#define AUDIODXE_PKG_VERSION  1

// Driver Bindings.
extern EFI_DRIVER_BINDING_PROTOCOL  gHdaControllerDriverBinding;
extern EFI_DRIVER_BINDING_PROTOCOL  gHdaCodecDriverBinding;
extern EFI_AUDIO_DECODE_PROTOCOL    gEfiAudioDecodeProtocol;

// Cannot raise TPL above this value round any code which calls gBS->SetTimer
// (using the DxeCore implementation of it, e.g. at least OVMF).
// REF: MdeModulePkg/Core/Dxe/Event/Timer.c
#define TPL_DXE_CORE_TIMER  (TPL_HIGH_LEVEL - 1)

// GPIO setup stages.
#define GPIO_SETUP_STAGE_DATA       BIT0
#define GPIO_SETUP_STAGE_DIRECTION  BIT1
#define GPIO_SETUP_STAGE_ENABLE     BIT2

#define GPIO_SETUP_STAGE_NONE  0

#define GPIO_SETUP_STAGE_ALL  (   \
  GPIO_SETUP_STAGE_DATA         | \
  GPIO_SETUP_STAGE_DIRECTION    | \
  GPIO_SETUP_STAGE_ENABLE       \
  )

// GPIO setup pin mask.
#define GPIO_PIN_MASK_AUTO  0              ///< Auto: use all reported available pins.

//
// Setup stage mask.
//
extern
UINTN
  gGpioSetupStageMask;

//
// GPIO pin mask.
//
extern
UINTN
  gGpioPinMask;

//
// Whether to restore NOSNOOPEN at exit.
//
extern
BOOLEAN
  gRestoreNoSnoop;

//
// Forced device path for HDA controller (ignore advertised class/subclass).
//
extern
EFI_DEVICE_PATH_PROTOCOL *
  gForcedControllerDevicePath;

//
// Time to wait in microseconds per codec for all widgets to fully come on.
//
extern
UINTN
  gCodecSetupDelay;

#endif // EFI_AUDIODXE_H
