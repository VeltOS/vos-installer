/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"
#include <libcmk/button.h>

struct _PageHome
{
	CmkWidget parent;
	
	CmkWidget *logo;
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

static void next_page(PageHome *self)
{
	ClutterActor *next = clutter_actor_get_next_sibling(CLUTTER_ACTOR(self));
	clutter_actor_hide(CLUTTER_ACTOR(self));
	clutter_actor_show(next);
}

static void page_home_init(PageHome *self)
{
	//self->logo = cmk_widget_new();
	//cmk_widget_set_background_color_name(logo, "accent");
	//clutter_actor_add_child(CLUTTER_ACTOR(bg), CLUTTER_ACTOR(logo));

	self->nextButton = cmk_button_new_full("Begin Installation", CMK_BUTTON_TYPE_BEVELED);
	g_signal_connect_swapped(self->nextButton, "activate", G_CALLBACK(next_page), self);
	cmk_widget_set_background_color_name(CMK_WIDGET(self->nextButton), "accent");
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nextButton));
}

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	PageHome *self = PAGE_HOME(self);

	gfloat width = clutter_actor_box_get_width(box);
	gfloat height = clutter_actor_box_get_height(box);

	gfloat minW, minH, natW, natH;
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->nextButton), &minW, &minH, &natW, &natH);
	
	ClutterActorBox nextButton = {
		width/2 - natW/2,
		height*2/3 - natH/2,
		width/2 + natW/2,
		height*2/3 + natH/2
	};

	clutter_actor_allocate(CLUTTER_ACTOR(self->nextButton), &nextButton, flags);

	CLUTTER_ACTOR_CLASS(page_home_parent_class)->allocate(self_, box, flags);
}
