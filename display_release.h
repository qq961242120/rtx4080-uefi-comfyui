/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
EFI_STATUS GuDisplayPreflight(EFI_SYSTEM_TABLE *,EFI_HANDLE,EFI_HANDLE);
EFI_STATUS GuDisplayRelease(VOID);
BOOLEAN GuDisplayReleased(VOID);
EFI_STATUS GuDisplayFinish(VOID);
BOOLEAN GuDisplayReadyForWindows(VOID);
BOOLEAN GuDisplayMutated(VOID);
