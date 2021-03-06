/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"
#include "sd-utils.h"
#include <string.h>

struct _PageBootSelect
{
	CmkWidget parent;

	CmkLabel *helpLabel;
	CmkScrollBox *driveListBox;
	CmkButton *selectedDriveButton;
	CmkButton *nextButton, *backButton;
	CmkButton *nbNextButton;
	gconstpointer driveMonitor;
};

static void on_dispose(GObject *self_);
static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_skip(PageBootSelect *self);
static void on_drive_added(StorageDevice *device, PageBootSelect *self);
static void on_drive_removed(StorageDevice *device, PageBootSelect *self);

StorageDevice *gSelectedBoot = NULL;
static PageBootSelect *pageBootSelect = NULL;
extern StorageDevice *gSelectedDevice;

G_DEFINE_TYPE(PageBootSelect, page_boot_select, CMK_TYPE_WIDGET);

CmkWidget * page_boot_select_new(void)
{
	return CMK_WIDGET(g_object_new(page_boot_select_get_type(), NULL));
}

static void page_boot_select_class_init(PageBootSelectClass *class)
{
	G_OBJECT_CLASS(class)->dispose = on_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = on_allocate;
}

static void page_boot_select_init(PageBootSelect *self)
{
	self->driveListBox = cmk_scroll_box_new(CLUTTER_SCROLL_HORIZONTALLY);
	ClutterBoxLayout *listLayout = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(listLayout, CLUTTER_ORIENTATION_HORIZONTAL); 
	clutter_box_layout_set_spacing(listLayout, 10);
	clutter_actor_set_x_align(CLUTTER_ACTOR(self->driveListBox), CLUTTER_ACTOR_ALIGN_CENTER);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->driveListBox), CLUTTER_LAYOUT_MANAGER(listLayout));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->driveListBox));

	self->helpLabel = cmk_label_new_full("Select an EFI System Partition (ESP) to install rEFInd Boot Manager.\nSkip this step to install your own boot manager.", TRUE);
	cmk_label_set_line_alignment(self->helpLabel, PANGO_ALIGN_CENTER);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->helpLabel));
	
	self->driveMonitor = monitor_storage_devices(
		(StorageDeviceAddedCb)on_drive_added,
		(StorageDeviceRemovedCb)on_drive_removed,
		self);
	
	self->nextButton = cmk_button_new_with_text("Use Selected ESP", CMK_BUTTON_TYPE_RAISED);
	cmk_widget_set_disabled(CMK_WIDGET(self->nextButton), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nextButton));
	g_signal_connect_swapped(self->nextButton, "activate", G_CALLBACK(cmk_widget_replace), self);
	
	self->nbNextButton = cmk_button_new_with_text("Skip", CMK_BUTTON_TYPE_FLAT);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nbNextButton));
	g_signal_connect_swapped(self->nbNextButton, "activate", G_CALLBACK(on_skip), self);
	
	self->backButton = cmk_button_new_with_text("Back", CMK_BUTTON_TYPE_FLAT);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->backButton));
	g_signal_connect_swapped(self->backButton, "activate", G_CALLBACK(cmk_widget_back), self);
	
	pageBootSelect = self;
}

static void on_dispose(GObject *self_)
{
	PageBootSelect *self = PAGE_BOOT_SELECT(self_);
	stop_monitoring_storage_devices(self->driveMonitor);
	G_OBJECT_CLASS(page_boot_select_parent_class)->dispose(self_);
}

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	PageBootSelect *self = PAGE_BOOT_SELECT(self_);

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
	
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->nbNextButton), &minW, &minH, &natW, &natH);
	
	ClutterActorBox nbNextButton = {
		nextButton.y2-pad/2 - natW,
		height-pad - natH,
		nextButton.y2-pad/2,
		height-pad
	};

	ClutterActorBox help = {
		pad,
		pad*2,
		width-pad,
		pad*2+hnatH
	};

	clutter_actor_get_preferred_height(CLUTTER_ACTOR(self->driveListBox), width, NULL, &natH);

	ClutterActorBox b = {0, height/2-natH/2-pad/2, width, height/2+natH/2+pad/2};

	clutter_actor_allocate(CLUTTER_ACTOR(self->helpLabel), &help, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->driveListBox), &b, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->nextButton), &nextButton, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->nbNextButton), &nbNextButton, flags);
	
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->backButton), &minW, &minH, &natW, &natH);
	ClutterActorBox backButton = {
		pad,
		height-pad - natH,
		pad + natW,
		height-pad
	};
	clutter_actor_allocate(CLUTTER_ACTOR(self->backButton), &backButton, flags);

	CLUTTER_ACTOR_CLASS(page_boot_select_parent_class)->allocate(self_, box, flags);
}

static void on_boot_select(CmkButton *driveButton, PageBootSelect *self)
{
	if(self->selectedDriveButton)
		cmk_button_set_selected(self->selectedDriveButton, FALSE);
	
	self->selectedDriveButton = driveButton;
	
	if(gSelectedBoot)
	{
		free_storage_device(gSelectedBoot);
		gSelectedBoot = NULL;
	}
	
	if(driveButton)
	{
		cmk_button_set_selected(driveButton, TRUE);
		
		gSelectedBoot = copy_storage_device(g_object_get_data(G_OBJECT(driveButton), "device"));
	}

	cmk_widget_set_disabled(CMK_WIDGET(self->nextButton), driveButton == NULL);
}

static void on_skip(PageBootSelect *self)
{
	on_boot_select(NULL, self);
	cmk_widget_replace(CMK_WIDGET(self), NULL);
}

static gboolean add_drive(StorageDevice *device)
{
	// Only show EFI
	if(!device->efi)
	{
		// Parameter 'device' is a copy of the device monitor's StorageDevice,
		// we must free it
		free_storage_device(device);
		return G_SOURCE_REMOVE;
	}
		
	PageBootSelect *self = (PageBootSelect *)device->userdata;
	
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
	
	CmkLabel *node = cmk_label_new_with_text(device->node);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(node));
	
	CmkLabel *recom = cmk_label_new_with_text("Recommended");
	cmk_label_set_bold(recom, TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(recom));
	g_object_set_data(G_OBJECT(button), "recom", recom);
	
	cmk_button_set_content(button, box);
	g_signal_connect(button, "activate", G_CALLBACK(on_boot_select), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self->driveListBox), CLUTTER_ACTOR(button));
	return G_SOURCE_REMOVE;
}

static gboolean remove_drive(StorageDevice *device)
{
	PageBootSelect *self = (PageBootSelect *)device->userdata;
	ClutterActorIter iter;
	ClutterActor *child;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self->driveListBox));
	while(clutter_actor_iter_next(&iter, &child))
	{
		if(g_strcmp0(clutter_actor_get_name(child), device->node) == 0)
		{
			if(self->selectedDriveButton == CMK_BUTTON(child))
				on_boot_select(NULL, self);
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
static void on_drive_added(StorageDevice *device, PageBootSelect *self)
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
static void on_drive_removed(StorageDevice *device, PageBootSelect *self)
{
	StorageDevice *copy = copy_storage_device(device);
	copy->userdata = self;
	clutter_threads_add_idle_full(
		G_PRIORITY_HIGH,
		(GSourceFunc)remove_drive,
		copy,
		NULL);
}

// Update the "recommended" flag. The installer recommends the ESP
// on the same physical drive as the installation drive.
// The user is not on the Boot Select page when this changes
void g_01b_selected_drive_changed(void)
{
	PageBootSelect *self = pageBootSelect;
	g_return_if_fail(PAGE_IS_BOOT_SELECT(self));

	ClutterActorIter iter;
	ClutterActor *child;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self->driveListBox));
	gboolean found = FALSE;
	while(clutter_actor_iter_next(&iter, &child))
	{
		StorageDevice *dev = g_object_get_data(G_OBJECT(child), "device");
		if(!dev) continue;
		
		CmkLabel *recom = g_object_get_data(G_OBJECT(child), "recom");
		
		if(!found
		&& gSelectedDevice
		&& g_strcmp0(gSelectedDevice->parent, dev->parent) == 0)
		{
			cmk_label_set_text(recom, "Recommended");
			found = TRUE;
		}
		else
		{
			cmk_label_set_text(recom, "");
		}
	}

	on_boot_select(NULL, self);
}
