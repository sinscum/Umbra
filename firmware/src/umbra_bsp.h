/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA: the build options are UMBRA_*; the hardware layer (firmware/bsp, a submodule we don't edit) reads its
 * own names. This hands each UMBRA_* to the name the BSP reads. Include it after the file's own UMBRA_* defaults
 * and before the first BSP file. Options the BSP gives a default get the same default here, so code that reads
 * UMBRA_<NAME> sees what the BSP sees. */
#ifndef UMBRA_BSP_H
#define UMBRA_BSP_H

/* set only when defined (the BSP tests them with #ifdef, or they have no BSP default) */
#ifdef UMBRA_LOADER
#define FELUCCA_LOADER UMBRA_LOADER
#endif
#ifdef UMBRA_ID
#define FELUCCA_ID UMBRA_ID
#endif
#ifdef UMBRA_FLASH
#define FELUCCA_FLASH UMBRA_FLASH
#endif
#ifdef UMBRA_OTA
#define FELUCCA_OTA UMBRA_OTA
#endif
#ifdef UMBRA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN UMBRA_OTA_DRYRUN
#endif
#ifdef UMBRA_UAC_TONE
#define FELUCCA_UAC_TONE UMBRA_UAC_TONE
#endif
#ifdef UMBRA_USB_MANUFACTURER
#define FELUCCA_USB_MANUFACTURER UMBRA_USB_MANUFACTURER
#endif
#ifdef UMBRA_USB_PRODUCT
#define FELUCCA_USB_PRODUCT UMBRA_USB_PRODUCT
#endif

/* with the BSP's defaults (bsp/chip/usb.c, ota.c) */
#ifndef UMBRA_USB_HOST
#define UMBRA_USB_HOST 0
#endif
#ifndef UMBRA_CDC
#define UMBRA_CDC 0
#endif
#ifndef UMBRA_UAC
#define UMBRA_UAC 0
#endif
#ifndef UMBRA_UAC_48K
#define UMBRA_UAC_48K 1
#endif
#ifndef UMBRA_UAC_OUT
#define UMBRA_UAC_OUT 0
#endif
#ifndef UMBRA_UAC_AS_FIRST
#define UMBRA_UAC_AS_FIRST 0
#endif
#ifndef UMBRA_UAC_DEEP
#define UMBRA_UAC_DEEP 0
#endif
#ifndef UMBRA_UAC_RING_ATTR
#define UMBRA_UAC_RING_ATTR
#endif
#ifndef UMBRA_MIRROR
#define UMBRA_MIRROR 0
#endif
#ifndef UMBRA_USB_LAYOUT
#define UMBRA_USB_LAYOUT 0
#endif
#ifndef UMBRA_CDC_DEFAULT
#define UMBRA_CDC_DEFAULT 1
#endif
#ifndef UMBRA_USB_PID
#define UMBRA_USB_PID 0x0001     /* the update loader is 0x0002 */
#endif
#ifndef UMBRA_OTA_RAMONLY
#define UMBRA_OTA_RAMONLY 0
#endif
#define FELUCCA_USB_HOST UMBRA_USB_HOST
#define FELUCCA_CDC UMBRA_CDC
#define FELUCCA_UAC UMBRA_UAC
#define FELUCCA_UAC_48K UMBRA_UAC_48K
#define FELUCCA_UAC_OUT UMBRA_UAC_OUT
#define FELUCCA_UAC_AS_FIRST UMBRA_UAC_AS_FIRST
#define FELUCCA_UAC_DEEP UMBRA_UAC_DEEP
#define FELUCCA_UAC_RING_ATTR UMBRA_UAC_RING_ATTR
#define FELUCCA_MIRROR UMBRA_MIRROR
#define FELUCCA_USB_LAYOUT UMBRA_USB_LAYOUT
#define FELUCCA_CDC_DEFAULT UMBRA_CDC_DEFAULT
#define FELUCCA_USB_PID UMBRA_USB_PID
#define FELUCCA_OTA_RAMONLY UMBRA_OTA_RAMONLY

#endif
