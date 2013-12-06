#ifndef PCI_H
#define PCI_H

#include <types.h>

#define PCI_GET_CLASS(x) ((x >> 0x18) & 0xFF)
#define PCI_GET_SUBCLASS(x) ((x >> 0x18) & 0xFF)
#define PCI_GET_PROGIF(x) ((x >> 0x8) & 0xFF)
#define PCI_GET_REVISION(x) (x & 0xFF)

// Used by drivers to register themselves for a class/vendor/device
typedef struct {
	uint16_t vendor;
	uint16_t device;
	uint32_t class;
	uint32_t class_mask;
} pci_ident_t;

typedef struct pci_loc {
	uint8_t bus, device, function;
} pci_loc_t;

typedef struct {
	pci_ident_t ident;

	uint32_t class;
} pci_function_t;

typedef struct {
	device_t d;

	pci_ident_t ident;
	pci_loc_t location;

	bool multifunction;

	pci_function_t function[8];
} pci_device_t;

typedef struct {
	device_t d;

	pci_ident_t ident;

	uint8_t bus_number;

	// The secondary bus is ONLY set if this is a bridge, otherwise it's 0xFFFF
	uint16_t bridge_secondary_bus;
} pci_bus_t;

bool pci_match(device_t* device, driver_t* driver);

#endif