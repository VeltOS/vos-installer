/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"
#include "sd-utils.h"
#include <string.h>

struct _PageDriveSelect
{
	CmkWidget parent;

	CmkLabel *helpLabel;
	CmkScrollBox *driveListBox;
	CmkButton *selectedDriveButton;
	CmkButton *nextButton;
	gconstpointer driveMonitor;
};

static void on_dispose(GObject *self_);
static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_drive_added(StorageDevice *device, PageDriveSelect *self);
static void on_drive_removed(StorageDevice *device, PageDriveSelect *self);

StorageDevice *gSelectedDevice;

G_DEFINE_TYPE(PageDriveSelect, page_drive_select, CMK_TYPE_WIDGET);

CmkWidget * page_drive_select_new(void)
{
	return CMK_WIDGET(g_object_new(page_drive_select_get_type(), NULL));
}

static void page_drive_select_class_init(PageDriveSelectClass *class)
{
	G_OBJECT_CLASS(class)->dispose = on_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = on_allocate;
}

static void page_drive_select_init(PageDriveSelect *self)
{
	self->driveListBox = cmk_scroll_box_new(CLUTTER_SCROLL_HORIZONTALLY);
	ClutterBoxLayout *listLayout = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(listLayout, CLUTTER_ORIENTATION_HORIZONTAL); 
	clutter_box_layout_set_spacing(listLayout, 10);
	clutter_actor_set_x_align(CLUTTER_ACTOR(self->driveListBox), CLUTTER_ACTOR_ALIGN_CENTER);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->driveListBox), CLUTTER_LAYOUT_MANAGER(listLayout));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->driveListBox));

	self->helpLabel = cmk_label_new_full("Please select a drive to install VeltOS on. All contents of the selected drive will be erased!", TRUE);
	cmk_label_set_line_alignment(self->helpLabel, PANGO_ALIGN_CENTER);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->helpLabel));
	
	self->driveMonitor = monitor_storage_devices(
		(StorageDeviceAddedCb)on_drive_added,
		(StorageDeviceRemovedCb)on_drive_removed,
		self);
	
	self->nextButton = cmk_button_new_with_text("Use Selected Drive", CMK_BUTTON_TYPE_RAISED);
	cmk_widget_set_disabled(CMK_WIDGET(self->nextButton), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nextButton));
	//g_signal_connect(self->nextButton, "activate", G_CALLBACK(on_next_select), self);
	g_signal_connect_swapped(self->nextButton, "activate", G_CALLBACK(cmk_widget_replace), self);
}

static void on_dispose(GObject *self_)
{
	PageDriveSelect *self = PAGE_DRIVE_SELECT(self_);
	stop_monitoring_storage_devices(self->driveMonitor);
	G_OBJECT_CLASS(page_drive_select_parent_class)->dispose(self_);
}

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	PageDriveSelect *self = PAGE_DRIVE_SELECT(self_);

	gfloat width = clutter_actor_box_get_width(box);
	gfloat height = clutter_actor_box_get_height(box);
	gfloat pad = CMK_DP(self_, 30);

	gfloat minW, minH, natW, natH;
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->nextButton), &minW, &minH, &natW, &natH);

	gfloat hminH, hnatH;
	clutter_actor_get_preferred_height(CLUTTER_ACTOR(self->helpLabel), width-pad*2, &hminH, &hnatH);
	
	ClutterActorBox nextButton = {
		width-pad - natW,
		height-pad - natH,
		width-pad,
		height-pad
	};

	ClutterActorBox help = {
		pad,
		pad*2,
		width-pad,
		pad*2+hnatH
	};

	ClutterActorBox b = {0,0,width,height};

	clutter_actor_allocate(CLUTTER_ACTOR(self->helpLabel), &help, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->driveListBox), &b, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->nextButton), &nextButton, flags);

	CLUTTER_ACTOR_CLASS(page_drive_select_parent_class)->allocate(self_, box, flags);
}

static void on_drive_select(CmkButton *driveButton, PageDriveSelect *self)
{
	if(self->selectedDriveButton)
	{
		//clutter_actor_save_easing_state(CLUTTER_ACTOR(self->selectedDriveButton));
		//clutter_actor_set_easing_duration(CLUTTER_ACTOR(self->selectedDriveButton), 1000);
		//cmk_widget_set_background_color(CMK_WIDGET(self->selectedDriveButton), "hover");
		//clutter_actor_restore_easing_state(CLUTTER_ACTOR(self->selectedDriveButton));
		cmk_button_set_selected(self->selectedDriveButton, FALSE);
	}
	self->selectedDriveButton = driveButton;
	cmk_button_set_selected(driveButton, TRUE);
	//clutter_actor_save_easing_state(CLUTTER_ACTOR(driveButton));
	//clutter_actor_set_easing_duration(CLUTTER_ACTOR(driveButton), 5000);
	//cmk_widget_set_background_color(CMK_WIDGET(driveButton), "accent");
	//clutter_actor_restore_easing_state(CLUTTER_ACTOR(driveButton));
	cmk_widget_set_disabled(CMK_WIDGET(self->nextButton), FALSE);

	if(gSelectedDevice)
	{
		free_storage_device(gSelectedDevice);
		gSelectedDevice = NULL;
	}

	gSelectedDevice = copy_storage_device(g_object_get_data(G_OBJECT(driveButton), "device"));
}

static gboolean add_drive(StorageDevice *device)
{
	// if(strncmp(device->fs, "ext4", 4))
	// 	return;
	
	// Don't show EFI System Partitions (if you want to install VOS onto your
	// EFI partition you should probably be using a different tool...)
	if(device->efi)
	{
		// Parameter 'device' is a copy of the device monitor's StorageDevice,
		// we must free it
		free_storage_device(device);
		return G_SOURCE_REMOVE;
	}
		
	PageDriveSelect *self = (PageDriveSelect *)device->userdata;
	
	const char *iconName = device->removable ? "drive-removable-media" : "drive-harddisk";

	CmkButton *button = cmk_button_new(CMK_BUTTON_TYPE_FLAT);
	g_object_set_data_full(G_OBJECT(button), "device", device, (GDestroyNotify)free_storage_device);
	clutter_actor_set_name(CLUTTER_ACTOR(button), device->node);
	cmk_button_set_type(button, CMK_BUTTON_TYPE_FLAT); 
	
	CmkWidget *box = cmk_widget_new();
	ClutterBoxLayout *list = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(list, CLUTTER_ORIENTATION_VERTICAL); 
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(box), CLUTTER_LAYOUT_MANAGER(list));
	
	CmkIcon *icon = cmk_icon_new_from_name(iconName, 96);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(icon));
	
	CmkLabel *label = cmk_label_new_with_text(device->name);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(label));
	
	CmkLabel *fs = cmk_label_new_with_text(device->fs);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(fs));
		
	cmk_button_set_content(button, box);
	g_signal_connect(button, "activate", G_CALLBACK(on_drive_select), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self->driveListBox), CLUTTER_ACTOR(button));
	return G_SOURCE_REMOVE;
}


static gboolean remove_drive(StorageDevice *device)
{
	PageDriveSelect *self = (PageDriveSelect *)device->userdata;
	ClutterActorIter iter;
	ClutterActor *child;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self->driveListBox));
	while(clutter_actor_iter_next(&iter, &child))
	{
		if(g_strcmp0(clutter_actor_get_name(child), device->node) == 0)
		{
			if(self->selectedDriveButton == CMK_BUTTON(child))
				self->selectedDriveButton = NULL;
			clutter_actor_destroy(child);
			break;
		}
	}
	// Parameter 'device' is a copy of the device monitor's StorageDevice,
	// we must free it
	free_storage_device(device);
	return G_SOURCE_REMOVE;
}

// Called from separate monitor thread
static void on_drive_added(StorageDevice *device, PageDriveSelect *self)
{
	StorageDevice *copy = copy_storage_device(device);
	copy->userdata = self;
	// clutter_threads_add_idle_full is Clutter's way of communicating from
	// a secondary thread to the main thread
	clutter_threads_add_idle_full(
		G_PRIORITY_HIGH,
		(GSourceFunc)add_drive,
		copy,
		NULL);
}

// Called from separate monitor thread
static void on_drive_removed(StorageDevice *device, PageDriveSelect *self)
{
	StorageDevice *copy = copy_storage_device(device);
	copy->userdata = self;
	clutter_threads_add_idle_full(
		G_PRIORITY_HIGH,
		(GSourceFunc)remove_drive,
		copy,
		NULL);
}
