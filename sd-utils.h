/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * Utilities for interfacing storage devices. Requires libudev.
 */

#include <glib.h>

/*
 * Contains info about single storage device partition.
 */
typedef struct {
	char *node; // '/dev/...'
	char *parent; // Dev path to parent device (eg parent device of '/dev/sda3' is '/dev/sda')
	char *name; // Human-readable name
	char *fs; // Filesystem name (ex 'ext4' or 'ntfs')
	long sizeBytes;
	int readOnly;
	int removable; // True for external devices like USB flash drives.
	int efi; // True if the partition is probably the EFI System Partition
	void *userdata;
} StorageDevice;

// Called on a separate thread from the caller of monitor_storage_devices.
typedef void (*StorageDeviceAddedCb)(StorageDevice *device, gpointer userdata);
typedef void (*StorageDeviceRemovedCb)(StorageDevice *device, gpointer userdata);

gconstpointer monitor_storage_devices(StorageDeviceAddedCb addcb, StorageDeviceRemovedCb removecb, gpointer userdata);
void stop_monitoring_storage_devices(gconstpointer monitor);

void free_storage_device(StorageDevice *device);
StorageDevice * copy_storage_device(const StorageDevice *device);
