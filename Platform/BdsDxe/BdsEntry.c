/** @file
  This module produce main entry for BDS phase - BdsEntry.
  When this module was dispatched by DxeCore, gEfiBdsArchProtocolGuid will be installed
  which contains interface of BdsEntry.
  After DxeCore finish DXE phase, gEfiBdsArchProtocolGuid->BdsEntry will be invoked
  to enter BDS phase.

Copyright (c) 2004 - 2014, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "Bds.h"

#include <Guid/GlobalVariable.h>
#include <Guid/EventGroup.h>

#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DuetBdsLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/VariableLock.h>

///
/// BDS arch protocol instance initial value.
///
EFI_HANDLE  gBdsHandle = NULL;

EFI_BDS_ARCH_PROTOCOL  gBds = {
  BdsEntry
};

///
/// The read-only variables defined in UEFI Spec.
///
CHAR16  *mReadOnlyVariables[] = {
  L"PlatformLangCodes",
  L"LangCodes",
  L"BootOptionSupport",
  L"HwErrRecSupport",
  L"OsIndicationsSupported"
  };

/**

  Install Boot Device Selection Protocol

  @param ImageHandle     The image handle.
  @param SystemTable     The system table.

  @retval  EFI_SUCEESS  BDS has finished initializing.
                        Return the dispatcher and recall BDS.Entry
  @retval  Other        Return status from AllocatePool() or gBS->InstallProtocolInterface

**/
EFI_STATUS
EFIAPI
BdsInitialize (
  IN EFI_HANDLE                            ImageHandle,
  IN EFI_SYSTEM_TABLE                      *SystemTable
  )
{
  EFI_STATUS  Status;

  //
  // Install protocol interface
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gBdsHandle,
                  &gEfiBdsArchProtocolGuid, &gBds,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

/**

  This function attempts to boot for the boot order specified
  by platform policy.

**/
STATIC
VOID
BdsBootDeviceSelect (
  VOID
  )
{
  EFI_STATUS                    Status;
  UINT16                        NonBlockNumber;
  UINTN                         Index;
  EFI_HANDLE                    *FileSystemHandles;
  UINTN                         NumberFileSystemHandles;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;
  EFI_HANDLE                    ImageHandle;

  //
  // Signal the EVT_SIGNAL_READY_TO_BOOT event
  //
  EfiSignalEventReadyToBoot ();

  //
  // If there is simple file protocol which does not consume block Io protocol, create a boot option for it here.
  //
  NonBlockNumber = 0;
  Status = gBS->LocateHandleBuffer (
    ByProtocol,
    &gEfiSimpleFileSystemProtocolGuid,
    NULL,
    &NumberFileSystemHandles,
    &FileSystemHandles
    );

  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < NumberFileSystemHandles; Index++) {
    //
    // Do the removable Media thing. \EFI\BOOT\boot{machinename}.EFI
    //  machinename is ia32, ia64, x64, ...
    //
    DevicePath = FileDevicePath (
      FileSystemHandles[Index],
      L"\\EFI\\OC\\Bootstrap\\Bootstrap.efi"
      );
  
    if (DevicePath == NULL) {
      continue;
    }

    ImageHandle = NULL;
    Status = gBS->LoadImage (
      TRUE,
      gImageHandle,
      DevicePath,
      NULL,
      0,
      &ImageHandle
      );

    if (!EFI_ERROR (Status)) {
      gBS->StartImage (
        ImageHandle,
        0,
        NULL
        );
    }

    FreePool (DevicePath);
  }

  if (NumberFileSystemHandles != 0) {
    FreePool (FileSystemHandles);
  }
}

/**

  Validate input console variable data. 

  If found the device path is not a valid device path, remove the variable.
  
  @param VariableName             Input console variable name.

**/
STATIC
VOID
BdsFormalizeConsoleVariable (
  IN  CHAR16          *VariableName
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  UINTN                     VariableSize;

  DevicePath = BdsLibGetVariableAndSize (
                      VariableName,
                      &gEfiGlobalVariableGuid,
                      &VariableSize
                      );
  if ((DevicePath != NULL) && !IsDevicePathValid (DevicePath, VariableSize)) { 
    Status = gRT->SetVariable (
                    VariableName,
                    &gEfiGlobalVariableGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                    0,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  }
}

/**

  Formalize Bds global variables. 

 1. For ConIn/ConOut/ConErr, if found the device path is not a valid device path, remove the variable.
 2. For OsIndicationsSupported, Create a BS/RT/UINT64 variable to report caps 
 3. Delete OsIndications variable if it is not NV/BS/RT UINT64
 Item 3 is used to solve case when OS corrupts OsIndications. Here simply delete this NV variable.

**/
STATIC
VOID 
BdsFormalizeEfiGlobalVariable (
  VOID
  )
{
  UINT64     OsIndicationSupport;
  
  //
  // Validate Console variable.
  //
  BdsFormalizeConsoleVariable (L"ConIn");
  BdsFormalizeConsoleVariable (L"ConOut");
  BdsFormalizeConsoleVariable (L"ErrOut");

  //
  // OS indicater support variable
  //
  OsIndicationSupport = EFI_OS_INDICATIONS_BOOT_TO_FW_UI \
                      | EFI_OS_INDICATIONS_FMP_CAPSULE_SUPPORTED;

  gRT->SetVariable (
    L"OsIndicationsSupported",
    &gEfiGlobalVariableGuid,
    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
    sizeof(UINT64),
    &OsIndicationSupport
    );

}

/**
  Set language related EFI Variables.

**/
STATIC
VOID
InitializeLanguage (
  VOID
  )
{
  CHAR8       *PlatformLangCodes;
  CHAR8       *PlatformLang;

  PlatformLangCodes = (CHAR8 *) PcdGetPtr (PcdUefiVariableDefaultPlatformLangCodes);
  PlatformLang      = (CHAR8 *) PcdGetPtr (PcdUefiVariableDefaultPlatformLang);

  gRT->SetVariable (
    L"PlatformLangCodes",
    &gEfiGlobalVariableGuid,
    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
    AsciiStrSize (PlatformLangCodes),
    PlatformLangCodes
    );


  gRT->SetVariable (
    L"PlatformLang",
    &gEfiGlobalVariableGuid,
    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
    AsciiStrSize (PlatformLang),
    PlatformLang
    );
}

/**

  Service routine for BdsInstance->Entry(). Devices are connected, the
  consoles are initialized, and the boot options are tried.

  @param This             Protocol Instance structure.

**/
VOID
EFIAPI
BdsEntry (
  IN EFI_BDS_ARCH_PROTOCOL  *This
  )
{
  CHAR16                          *FirmwareVendor;
  EFI_STATUS                      Status;
  UINT16                          BootTimeOut;
  UINTN                           Index;
  EDKII_VARIABLE_LOCK_PROTOCOL    *VariableLock;

  //
  // Fill in FirmwareVendor and FirmwareRevision from PCDs
  //
  FirmwareVendor = (CHAR16 *) PcdGetPtr (PcdFirmwareVendor);
  gST->FirmwareVendor = AllocateRuntimeCopyPool (StrSize (FirmwareVendor), FirmwareVendor);
  ASSERT (gST->FirmwareVendor != NULL);
  gST->FirmwareRevision = PcdGet32 (PcdFirmwareRevision);

  //
  // Fixup Table CRC after we updated Firmware Vendor and Revision
  //
  gST->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gST, sizeof (EFI_SYSTEM_TABLE), &gST->Hdr.CRC32);

  //
  // Validate Variable.
  // TODO: Explore.
  //
  BdsFormalizeEfiGlobalVariable ();

  //
  // Mark the read-only variables if the Variable Lock protocol exists
  //
  Status = gBS->LocateProtocol (&gEdkiiVariableLockProtocolGuid, NULL, (VOID **) &VariableLock);
  DEBUG ((EFI_D_INFO, "[BdsDxe] Locate Variable Lock protocol - %r\n", Status));
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < sizeof (mReadOnlyVariables) / sizeof (mReadOnlyVariables[0]); Index++) {
      Status = VariableLock->RequestToLock (VariableLock, mReadOnlyVariables[Index], &gEfiGlobalVariableGuid);
      ASSERT_EFI_ERROR (Status);
    }
  }

  //
  // Initialize L"Timeout" EFI global variable.
  //
  BootTimeOut = 0;
  gRT->SetVariable (
    L"Timeout",
    &gEfiGlobalVariableGuid,
    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
    sizeof (UINT16),
    &BootTimeOut
    );

  //
  // Platform specific code
  // Initialize the platform specific string and language
  //
  InitializeLanguage ();

  //
  // Do the platform init, can be customized by OEM/IBV
  //
  PlatformBdsInit ();

  //
  // Signal EndOfDxe
  //
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);

  //
  // Setup some platform policy here
  //
  PlatformBdsPolicyBehavior ();

  //
  // BDS select the boot device to load OS
  //
  BdsBootDeviceSelect ();

  //
  // Show something meaningful.
  //
  gST->ConOut->OutputString (gST->ConOut, L"FAILED TO BOOT\r\n");
  gBS->Stall (3000000);
  CpuDeadLoop ();

  //
  // Only assert here since this is the right behavior, we should never
  // return back to DxeCore.
  //
  ASSERT (FALSE);
}