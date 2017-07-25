/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"
#include <string.h>

struct _PageComplete
{
	CmkWidget parent;
};

static void on_dispose(GObject *self_);
static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_next_button_activate(PageComplete *self);

G_DEFINE_TYPE(PageComplete, page_complete, CMK_TYPE_WIDGET);

CmkWidget * page_complete_new(void)
{
	return CMK_WIDGET(g_object_new(page_complete_get_type(), NULL));
}

static void page_complete_class_init(PageCompleteClass *class)
{
	//G_OBJECT_CLASS(class)->dispose = on_dispose;
	//CLUTTER_ACTOR_CLASS(class)->allocate = on_allocate;
}

static void page_complete_init(PageComplete *self)
{
}

void spawn_installer_process(const gchar *drive, const gchar *name, const gchar *username, const gchar *hostname, const gchar *password)
{
	g_message("spawn %s, %s, %s, %s, %s", drive, name, username, hostname, password);
}
