/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * Utilities for interfacing storage devices. Requires libudev.
 */

#include "sd-utils.h"
#include <libudev.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	gboolean stop;
	gpointer userdata;
	StorageDeviceAddedCb addcb;
	StorageDeviceRemovedCb removecb;
} MonitorThreadData;

static void monitor_thread(MonitorThreadData *monitor);

gconstpointer monitor_storage_devices(StorageDeviceAddedCb addcb, StorageDeviceRemovedCb removecb, gpointer userdata)
{
	MonitorThreadData *monitor = g_new(MonitorThreadData, 1);
	monitor->stop = FALSE;
	monitor->userdata = userdata;
	monitor->addcb = addcb;
	monitor->removecb = removecb;
	GThread *t = g_thread_try_new("sdmonitor", (GThreadFunc)monitor_thread, monitor, NULL);
	if(t == NULL) { g_free(monitor); return NULL; }
	return monitor;
}

void stop_monitoring_storage_devices(gconstpointer monitor)
{
	g_return_if_fail(monitor != NULL);
	((MonitorThreadData *)monitor)->stop = TRUE;
}

static void add_drive_device(MonitorThreadData *monitor, GArray *drives, struct udev_device *dev)
{
	const char *devType = udev_device_get_property_value(dev, "DEVTYPE");
	if(devType == NULL || strncmp(devType, "partition", 9) != 0)
		return;
	
	const gchar *name = udev_device_get_property_value(dev, "ID_FS_LABEL");
	if(!name)
		name = udev_device_get_property_value(dev, "PARTNAME");
	if(!name)
		return;
	
	StorageDevice sd;
	sd.node = g_strdup(udev_device_get_devnode(dev));
	sd.name = g_strdup(name);
	sd.fs = g_strdup(udev_device_get_property_value(dev, "ID_FS_TYPE"));
	// g_message("adding drive: %s, %s, %s", sd.node, sd.name, sd.fs);
	
	// According to the linux documentation, a sector is always 512 bytes
	// https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/include/linux/types.h?id=v4.4-rc6#n121
	const char *sectors = udev_device_get_property_value(dev, "ID_PART_ENTRY_SIZE");
	long long sizeBytes = strtoll(sectors, NULL, 10) * 512;
	sd.sizeBytes = sizeBytes;
	
	const char *readOnlyStr = udev_device_get_sysattr_value(dev, "ro");
	sd.readOnly = 0;
	if(readOnlyStr)
		sd.readOnly = strtol(readOnlyStr, NULL, 10) ? 1 : 0;
	
	// Test if the device is removable (ex USB) by checking its parent's
	// 'removable' value. (A parent is the main drive device, so if the
	// device is /dev/sda1, the parent is /dev/sda.)
	sd.removable = 0;
	struct udev_device *parent = udev_device_get_parent(dev);
	if(parent)
	{
		const char *removable = udev_device_get_sysattr_value(parent, "removable");
		if(removable)
			sd.removable = strtol(removable, NULL, 10) ? 1 : 0;
	}

	sd.parent = g_strdup(udev_device_get_devnode(parent));
	
	// Check for EFI System Partition
	char *partType = g_ascii_strdown(udev_device_get_property_value(dev, "ID_PART_ENTRY_TYPE"), -1);
	const char *partTableType = udev_device_get_property_value(dev, "ID_PART_TABLE_TYPE");
	
	sd.efi = 0;
	if(partType && partTableType)
	{
		// c12a73... is the standard partition type GUID for EFI System Partitions
		static const char *espGUID = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";
		
		sd.efi = (strncmp(partType, espGUID, sizeof(espGUID)-1) == 0
		       && strncmp(partTableType, "gpt", 3) == 0);
	}
	
	g_free(partType);

	g_array_append_val(drives, sd);
	monitor->addcb(&g_array_index(drives, StorageDevice, drives->len-1), monitor->userdata);

	// For debug -- lists all properties on the device
	// struct udev_list_entru *sysentry;
	// struct udev_list_entry *sysattrs = udev_device_get_properties_list_entry(dev);
	// 
	// udev_list_entry_foreach(sysentry, sysattrs)
	// {
	// 	const char *entryName = udev_list_entry_get_name(sysentry);
	// 	printf("%s: %s\n", entryName, udev_device_get_property_value(dev, entryName));
	// }
	// printf("---\n");
}

static void remove_drive_device(MonitorThreadData *monitor, GArray *drives, struct udev_device *dev)
{
	for(guint i=0;i<drives->len;++i)
	{
		StorageDevice *d = &g_array_index(drives, StorageDevice, i);
		if(g_strcmp0(d->node, udev_device_get_devnode(dev)) == 0)
		{
			monitor->removecb(d, monitor->userdata);
			g_array_remove_index_fast(drives, i);
			return;
		}
	}
}

static void check_drive_device_action(MonitorThreadData *monitor, GArray *drives, struct udev_device *dev)
{
	const gchar *action = udev_device_get_action(dev);
	if(g_strcmp0(action, "add") == 0)
		add_drive_device(monitor, drives, dev);
	else if(g_strcmp0(action, "remove") == 0)
		remove_drive_device(monitor, drives, dev);
	else if(g_strcmp0(action, "change") == 0)
	{
		remove_drive_device(monitor, drives, dev);
		add_drive_device(monitor, drives, dev);
	}
}

static void free_storage_device_contents(StorageDevice *device)
{
	g_return_if_fail(device);
	g_free(device->node);
	g_free(device->parent);
	g_free(device->name);
	g_free(device->fs);
}

static void monitor_thread(MonitorThreadData *monitor)
{
	g_return_if_fail(monitor);
	struct udev *udev = udev_new();
	g_return_if_fail(udev);
	
	GArray *drives = g_array_new(FALSE, FALSE, sizeof(StorageDevice));
	g_array_set_clear_func(drives, (GDestroyNotify)free_storage_device_contents);
	
	// Start monitoring for new devices
	struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "block", NULL);
	udev_monitor_enable_receiving(mon);
	int fd = udev_monitor_get_fd(mon);
	
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "block");
	udev_enumerate_scan_devices(enumerate);
	
	struct udev_list_entry *devListEntry;
	struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(devListEntry, devices)
	{
		const char *path = udev_list_entry_get_name(devListEntry);
		struct udev_device *dev = udev_device_new_from_syspath(udev, path);
		add_drive_device(monitor, drives, dev);
		udev_device_unref(dev);
	}
	
	while(!monitor->stop)
	{
		// Block until devices are available
		fd_set fds;
		struct timeval tv;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 2; // timeout 2 seconds to check for monitor->stop
		tv.tv_usec = 0;
		select(fd+1, &fds, NULL, NULL, &tv);
		
		if(monitor->stop) break;
		
		// Get all available devices
		struct udev_device *dev = NULL;
		while((dev = udev_monitor_receive_device(mon)) != NULL)
		{
			check_drive_device_action(monitor, drives, dev);
			udev_device_unref(dev);
		}
	}
	
	g_array_unref(drives);
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	g_free(monitor);
}

void free_storage_device(StorageDevice *device)
{
	free_storage_device_contents(device);
	g_free(device);
}

StorageDevice * copy_storage_device(const StorageDevice *device)
{
	if(!device)
		return NULL;
	StorageDevice *copy = g_new0(StorageDevice, 1);
	copy->node = g_strdup(device->node);
	copy->parent = g_strdup(device->parent);
	copy->name = g_strdup(device->name);
	copy->fs = g_strdup(device->fs);
	copy->sizeBytes = device->sizeBytes;
	copy->readOnly = device->readOnly;
	copy->removable = device->removable;
	copy->efi = device->efi;
	copy->userdata = device->userdata;
	return copy;
}
