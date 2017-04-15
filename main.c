/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include <libcmk/cmk-util.h>
#include <libcmk/cmk-label.h>
#include <libcmk/cmk-icon-loader.h>
#include "pages.h"

static const ClutterColor GrapheneColors[] = {
	{84, 110, 122, 255}, // background (panel)
	{255, 255, 255, 204}, // foreground (font)
	{255, 255, 255, 40}, // hover
	{255, 255, 255, 25}, // selected
	{208, 39, 39, 255}, // accent (vosred, #D02727)
};

static const float GrapheneBevelRadius = 3.0;
static const float GraphenePadding = 10.0;

int main(int argc, char **argv)
{
	cmk_disable_system_guiscale();
	if(clutter_init(&argc, &argv) != CLUTTER_INIT_SUCCESS)
		return 1;

	CmkWidget *window = cmk_window_new(800, 500);
	g_signal_connect(window, "destroy", G_CALLBACK(clutter_main_quit), NULL);

	CmkWidget *style = cmk_widget_get_style_default();
	cmk_widget_style_set_color(style, "background", &GrapheneColors[0]);
	cmk_widget_style_set_color(style, "foreground", &GrapheneColors[1]);
	cmk_widget_style_set_color(style, "accent", &GrapheneColors[4]);
	cmk_widget_style_set_color(style, "hover", &GrapheneColors[2]);
	cmk_widget_style_set_color(style, "selected", &GrapheneColors[3]);
	cmk_widget_style_set_bevel_radius(style, GrapheneBevelRadius);
	cmk_widget_style_set_padding(style, GraphenePadding);
	g_object_unref(style);
	
	//CmkLabel *label = cmk_label_new();
	//cmk_label_set_text(label, "The quick brown fox jumps over the lazy dog.");
	//clutter_actor_add_child(CLUTTER_ACTOR(window), CLUTTER_ACTOR(label));

	CmkWidget *home = page_home_new();
	clutter_actor_add_child(CLUTTER_ACTOR(window), CLUTTER_ACTOR(home));
	clutter_actor_add_constraint(CLUTTER_ACTOR(home), clutter_bind_constraint_new(CLUTTER_ACTOR(window), CLUTTER_BIND_ALL, 0));
	
	CmkWidget *ds = page_drive_select_new();
	clutter_actor_add_child(CLUTTER_ACTOR(window), CLUTTER_ACTOR(ds));
	clutter_actor_add_constraint(CLUTTER_ACTOR(ds), clutter_bind_constraint_new(CLUTTER_ACTOR(window), CLUTTER_BIND_ALL, 0));
	clutter_actor_hide(CLUTTER_ACTOR(ds));

	clutter_main();
	return 0;
}
