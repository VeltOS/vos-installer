/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"

struct _PageHome
{
	CmkWidget parent;
	
	CmkIcon *logo;
	CmkButton *nextButton;
};

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
G_DEFINE_TYPE(PageHome, page_home, CMK_TYPE_WIDGET);

CmkWidget * page_home_new(void)
{
	return CMK_WIDGET(g_object_new(page_home_get_type(), NULL));
}

static void page_home_class_init(PageHomeClass *class)
{
	CLUTTER_ACTOR_CLASS(class)->allocate = on_allocate;
}

static void page_home_init(PageHome *self)
{
	self->logo = cmk_icon_new_full("velt", "hicolor", 256, FALSE);
	clutter_actor_set_opacity(CLUTTER_ACTOR(self->logo), 180);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->logo));

	self->nextButton = cmk_button_new_with_text("Begin Installation", CMK_BUTTON_TYPE_RAISED);
	// main.c ignores the replacement argument, so just replace with button
	g_signal_connect_swapped(self->nextButton, "activate", G_CALLBACK(cmk_widget_replace), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nextButton));
}

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	PageHome *self = PAGE_HOME(self_);

	gfloat width = clutter_actor_box_get_width(box);
	gfloat height = clutter_actor_box_get_height(box);

	gfloat minW, minH, natW, natH;
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->nextButton), &minW, &minH, &natW, &natH);
	
	gfloat logoSize;
	clutter_actor_get_preferred_width(CLUTTER_ACTOR(self->logo), -1, NULL, &logoSize);

	ClutterActorBox nextButton = {
		(gint)(width/2 - natW/2),
		(gint)(height*3/4 - natH/2),
		width/2 + natW/2,
		height*3/4 + natH/2
	};
	
	ClutterActorBox logo = {
		width/2 - logoSize/2,
		height*1/3 - logoSize/2,
		width/2 + logoSize/2,
		height*1/3 + logoSize/2
	};
	
	clutter_actor_allocate(CLUTTER_ACTOR(self->nextButton), &nextButton, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->logo), &logo, flags);

	CLUTTER_ACTOR_CLASS(page_home_parent_class)->allocate(self_, box, flags);
}
