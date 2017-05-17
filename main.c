/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include <libcmk/cmk.h>
#include "pages.h"

static const CmkNamedColor GrapheneColors[] = {
	{"background", {84,  110, 122, 255}},
	{"foreground", {255, 255, 255, 204}},
	{"hover",      {255, 255, 255, 40}},
	{"selected",   {255, 255, 255, 25}},
	{"accent",     {208, 39,  39,  255}}, // vosred, #D02727
	NULL
};

static const float GrapheneBevelRadius = 3.0;
static const float GraphenePadding = 10.0;

CmkWidget *window;
static void next_page(ClutterActor *current)
{
	ClutterActor *next = clutter_actor_get_next_sibling(current);
	clutter_actor_hide(current);
	clutter_actor_show(next);
}

int main(int argc, char **argv)
{
	cmk_auto_dpi_scale();
	if(!cmk_init(&argc, &argv))
		return 1;
	
	CmkWidget *style = cmk_widget_get_style_default();
	cmk_widget_style_set_colors(style, GrapheneColors);
	cmk_widget_style_set_bevel_radius(style, GrapheneBevelRadius);
	cmk_widget_style_set_padding(style, GraphenePadding);
	
	CmkWidget *window = cmk_window_new("Velt Installer", 800, 600, NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(clutter_main_quit), NULL);
	
	CmkWidget *home = page_home_new();
	cmk_widget_add_child(window, home);
	cmk_widget_bind_fill(home);
	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
	
	CmkWidget *ds = page_drive_select_new();
	cmk_widget_add_child(window, ds);
	cmk_widget_bind_fill(ds);
	clutter_actor_hide(CLUTTER_ACTOR(ds));
	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
	
	cmk_main();
	return 0;
}
