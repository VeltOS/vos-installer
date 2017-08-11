/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"
#include "sd-utils.h"
#include <string.h>

struct _PageProfile
{
	CmkWidget parent;
	CmkScrollBox *container;
	CmkButton *nextButton, *backButton;
	CmkTextfield *name, *hostname, *username, *password, *passwordValidate;
};

static void on_dispose(GObject *self_);
static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_next_button_activate(PageProfile *self);
static gboolean validate_input(PageProfile *self, CmkTextfield *caller);

G_DEFINE_TYPE(PageProfile, page_profile, CMK_TYPE_WIDGET);

CmkWidget * page_profile_new(void)
{
	return CMK_WIDGET(g_object_new(page_profile_get_type(), NULL));
}

static void page_profile_class_init(PageProfileClass *class)
{
	G_OBJECT_CLASS(class)->dispose = on_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = on_allocate;
}

static ClutterLayoutManager * vbox(void)
{
	ClutterLayoutManager *m = clutter_box_layout_new();
	clutter_box_layout_set_orientation(CLUTTER_BOX_LAYOUT(m), CLUTTER_ORIENTATION_VERTICAL);
	return m;
}

static void page_profile_init(PageProfile *self)
{
	self->container = cmk_scroll_box_new(CLUTTER_SCROLL_BOTH);
	
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->container), vbox());
	CmkLabel *header = cmk_label_new_full("Create your profile", TRUE);
	cmk_label_set_line_alignment(header, PANGO_ALIGN_CENTER);
	cmk_widget_set_margin(CMK_WIDGET(header), 60, 60, 60, 10);
	cmk_widget_add_child(CMK_WIDGET(self->container), CMK_WIDGET(header));

	CmkWidget *box = cmk_widget_new();
	ClutterLayoutManager *m = clutter_box_layout_new();
	clutter_box_layout_set_orientation(CLUTTER_BOX_LAYOUT(m), CLUTTER_ORIENTATION_HORIZONTAL);
	clutter_box_layout_set_homogeneous(CLUTTER_BOX_LAYOUT(m), TRUE);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(box), m);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(box), TRUE);
	cmk_widget_add_child(CMK_WIDGET(self->container), box);

	// Put all the fields inside a container so that when they use
	// x-expand, they all expand to the width of the largest textfield.
	// That way, they're always all the same width.
	CmkWidget *left = cmk_widget_new();
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(left), vbox());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(left), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(left));
	
	CmkWidget *right = cmk_widget_new();
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(right), vbox());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(right), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(box), CLUTTER_ACTOR(right));
	
	CmkTextfield *z = cmk_textfield_new("Your Name (optional)", "");
	clutter_actor_set_x_expand(CLUTTER_ACTOR(z), TRUE);
	cmk_widget_set_margin(CMK_WIDGET(z), 30, 15, 0, 0);
	clutter_actor_add_child(CLUTTER_ACTOR(left), CLUTTER_ACTOR(z));
	self->name = z;

	z = cmk_textfield_new("Computer Name", "Also known as hostname");
	clutter_actor_set_x_expand(CLUTTER_ACTOR(z), TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(z), CLUTTER_ACTOR_ALIGN_FILL);
	cmk_widget_set_margin(CMK_WIDGET(z), 30, 15, 0, 0);
	clutter_actor_add_child(CLUTTER_ACTOR(left), CLUTTER_ACTOR(z));
	g_signal_connect_swapped(z, "changed", G_CALLBACK(validate_input), self);
	self->hostname = z;

	// TODO: Select location
	//z = cmk_dropdown_new();
	//cmk_widget_set_margin(CMK_WIDGET(z), 30, 15, 20, 0);
	//clutter_actor_set_x_expand(CLUTTER_ACTOR(z), TRUE);
	//clutter_actor_add_child(CLUTTER_ACTOR(left), CLUTTER_ACTOR(z));

	z = cmk_textfield_new("Username", "Your default user account");
	clutter_actor_set_x_expand(CLUTTER_ACTOR(z), TRUE);
	cmk_widget_set_margin(CMK_WIDGET(z), 15, 30, 0, 0);
	clutter_actor_add_child(CLUTTER_ACTOR(right), CLUTTER_ACTOR(z));
	g_signal_connect_swapped(z, "changed", G_CALLBACK(validate_input), self);
	self->username = z;
	
	z = cmk_textfield_new("Password", "For both the default user and root accounts");
	cmk_textfield_set_is_password(z, TRUE);
	cmk_widget_set_margin(CMK_WIDGET(z), 15, 30, 0, 0);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(z), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(right), CLUTTER_ACTOR(z));
	g_signal_connect_swapped(z, "changed", G_CALLBACK(validate_input), self);
	self->password = z;
	
	z = cmk_textfield_new("Confirm Password", NULL);
	cmk_textfield_set_is_password(z, TRUE);
	cmk_widget_set_margin(CMK_WIDGET(z), 15, 30, 0, 0);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(z), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(right), CLUTTER_ACTOR(z));
	g_signal_connect_swapped(z, "changed", G_CALLBACK(validate_input), self);
	self->passwordValidate = z;

	// Make the tab order flow from left to right instead of top to bottom
	cmk_widget_set_tab_next(CMK_WIDGET(self->name), CMK_WIDGET(self->username), NULL);
	cmk_widget_set_tab_next(CMK_WIDGET(self->username), CMK_WIDGET(self->hostname), CMK_WIDGET(self->name));
	cmk_widget_set_tab_next(CMK_WIDGET(self->hostname), CMK_WIDGET(self->password), CMK_WIDGET(self->username));
	cmk_widget_set_tab_next(CMK_WIDGET(self->password), NULL, CMK_WIDGET(self->hostname));
	
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->container));
	
	self->nextButton = cmk_button_new_with_text("Install VeltOS", CMK_BUTTON_TYPE_RAISED);
	cmk_widget_set_disabled(CMK_WIDGET(self->nextButton), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nextButton));
	g_signal_connect_swapped(self->nextButton, "activate", G_CALLBACK(on_next_button_activate), self);
	
	self->backButton = cmk_button_new_with_text("Back", CMK_BUTTON_TYPE_FLAT);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->backButton));
	g_signal_connect_swapped(self->backButton, "activate", G_CALLBACK(cmk_widget_back), self);
}

static void on_dispose(GObject *self_)
{
	G_OBJECT_CLASS(page_profile_parent_class)->dispose(self_);
}

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	CLUTTER_ACTOR_CLASS(page_profile_parent_class)->allocate(self_, box, flags | CLUTTER_DELEGATE_LAYOUT);
	PageProfile *self = PAGE_PROFILE(self_);

	gfloat width = clutter_actor_box_get_width(box);
	gfloat height = clutter_actor_box_get_height(box);
	gfloat pad = CMK_DP(self_, 30);

	gfloat minW, minH, natW, natH;
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->nextButton), &minW, &minH, &natW, &natH);
	
	ClutterActorBox container = {
		0,
		0,
		width,
		height-pad-natH-pad/2
	};
	clutter_actor_allocate(CLUTTER_ACTOR(self->container), &container, flags);
	
	ClutterActorBox nextButton = {
		width-pad - natW,
		height-pad - natH,
		width-pad,
		height-pad
	};
	
	clutter_actor_allocate(CLUTTER_ACTOR(self->nextButton), &nextButton, flags);

	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->backButton), &minW, &minH, &natW, &natH);
	ClutterActorBox backButton = {
		pad,
		height-pad - natH,
		pad + natW,
		height-pad
	};
	
	clutter_actor_allocate(CLUTTER_ACTOR(self->backButton), &backButton, flags);
}

extern StorageDevice *gSelectedDevice;
extern StorageDevice *gSelectedBoot;
extern void spawn_installer_process(const gchar *drive, const gchar *boot, const gchar *name, const gchar *username, const gchar *hostname, const gchar *password);

static void on_confirm_dialog_select(PageProfile *self, const gchar *selection)
{
	// This is a potentially data-destroying choice, so be super careful
	// that the user has selected the option and that nothing has changed
	// while the dialog is open.
	gchar *b = g_strdup_printf("Install to %s", gSelectedDevice->node);
	if(strlen(b) == strlen(selection) && g_strcmp0(selection, b) == 0)
	{
		cmk_widget_replace(CMK_WIDGET(self), NULL);
		spawn_installer_process(gSelectedDevice->node,
			gSelectedBoot ? gSelectedBoot->node : NULL,
			cmk_textfield_get_text(self->name),
			cmk_textfield_get_text(self->username),
			cmk_textfield_get_text(self->hostname),
			cmk_textfield_get_text(self->password));
	}
	g_free(b);
}

static void on_next_button_activate(PageProfile *self)
{
	if(validate_input(self, NULL))
	{
		gchar *l = g_strdup_printf("You are about to install VeltOS to\n\n  \"%s\" (%s)%s%s\n\nThis will PERMANENTLY DESTROY ALL DATA on the drive. Are you sure you want to continue?\n",
			gSelectedDevice->name,
			gSelectedDevice->node,
			gSelectedBoot ? "\n  with rEFInd at " : "",
			gSelectedBoot ? gSelectedBoot->node : "");
		gchar *b = g_strdup_printf("Install to %s", gSelectedDevice->node);
		CmkDialog *d = cmk_dialog_new_simple(l, NULL, "STOP!", b, NULL);
		g_signal_connect_swapped(d, "select", G_CALLBACK(on_confirm_dialog_select), self);
		g_free(b);
		g_free(l);
		cmk_dialog_show(d, CMK_WIDGET(self));
	}
}

static const gchar * validate_hostname(const gchar *hostname)
{
	// RFC 1123
	guint len = strlen(hostname);
	if(len == 0)
		return "Invalid hostname";
	else if(len > 63)
		return "Maximum of 63 characters";
	if(!g_ascii_isalnum(hostname[0]))
		return "First character must be alphanumeric";
	for(guint i=0;i<len;++i)
	{
		if(!g_ascii_isalnum(hostname[i]) && hostname[i] != '-')
			return "Only letters, digits, and - are allowed";
	}
	return NULL;
}

static const gchar * validate_username(const gchar *username)
{
	// The restrictions for username seem to vary by distro,
	// but generally it's something like what's specified in the
	// caveats section at
	// http://man7.org/linux/man-pages/man8/useradd.8.html
	// except this check allows uppercase letters.
	guint len = strlen(username);
	if(len == 0)
		return "Invalid username";
	else if(len > 31)
		return "Maximum of 31 characters";
	if(!g_ascii_isalnum(username[0]) && username[0] != '_')
		return "First character must be alphanumeric or _";
	for(guint i=0;i<len;++i)
	{
		if(!g_ascii_isalnum(username[i])
		&& username[i] != '-'
		&& username[i] != '_')
			return "Only letters, digits, -, and _ are allowed";
	}
	return NULL;
}

static gboolean validate_input(PageProfile *self, UNUSED CmkTextfield *caller)
{
	const gchar *hostname = cmk_textfield_get_text(self->hostname);
	const gchar *validateHostname = validate_hostname(hostname);
	if(strlen(hostname) == 0)
		cmk_textfield_set_error(self->hostname, NULL);
	else
		cmk_textfield_set_error(self->hostname, validateHostname);
	
	const gchar *username = cmk_textfield_get_text(self->username);
	const gchar *validateUsername = validate_username(username);
	if(strlen(username) == 0)
		cmk_textfield_set_error(self->username, NULL);
	else
		cmk_textfield_set_error(self->username, validateUsername);
	
	const gchar *pass = cmk_textfield_get_text(self->password);
	const gchar *passValidate = cmk_textfield_get_text(self->passwordValidate);
	gboolean validPassword = strlen(passValidate) == 0 || (strlen(pass) == strlen(passValidate) && g_strcmp0(pass, passValidate) == 0);
	if(validPassword)
		cmk_textfield_set_error(self->passwordValidate, NULL);
	else
		cmk_textfield_set_error(self->passwordValidate, "Passwords do not match");
	validPassword = validPassword && strlen(passValidate) > 0 && strlen(pass) > 0;
	
	gboolean allValid = validPassword && (validateUsername == NULL) && (validateHostname == NULL);
	cmk_widget_set_disabled(CMK_WIDGET(self->nextButton), !allValid);
	return allValid;
}
