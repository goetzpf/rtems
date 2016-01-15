/* Copyright 2014, Kate Feng  <kate007.feng@gmail.com>  All rights reserved
 * Find a PCI device by the vendor ID
 *
 * Acknowledgement to Till Straumann <strauman@slac.stanford.edu>, Copyright 2001
 * Comments from Kate Feng :  This file is a major overhaul of the original
 * pcifinddevice.c, Till Straumann, Copyright 2001
 *
 *
 */

#define PCI_MULTI_FUNCTIONS     0x80
#define PCI_INVALID_VENDORID    0xffff

#include <bsp/pci.h>
#include <rtems/bspIo.h>
#include <bsp.h>

int BSP_pciFindDevice( unsigned short vendorId, unsigned short *deviceId, int unit,
                         int *pbus, int *pdevice, int *pfunc)
{
  unsigned short vId,dId;
  unsigned char headerType, bus,device,func;

  for (bus=0; bus<BSP_MAX_PCI_BUS;  bus++) {
      for (device=0; device<PCI_MAX_DEVICES; device++) {
          pci_read_config_byte(bus, device, 0, PCI_HEADER_TYPE, &headerType);
          headerType = (headerType & PCI_MULTI_FUNCTIONS ? PCI_MAX_FUNCTIONS : 1);
          for (func=0; func<headerType; func++) {
              (void)pci_read_config_word(bus,device,func,PCI_VENDOR_ID,&vId);
              if ( vId== PCI_INVALID_VENDORID) continue;
              if ( vId != vendorId ) continue;
              (void)pci_read_config_word(bus,device,func,PCI_DEVICE_ID,&dId);
              if ( --unit) continue;
              *deviceId= dId;
              *pbus=bus;
              *pdevice=device;
              *pfunc=func;
              return 0;
          }
      }
  }  /* end for bus */
  return -1;
}

