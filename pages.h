/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __PAGES_H__
#define __PAGES_H__

#include <cmk/cmk.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(PageHome, page_home, PAGE, HOME, CmkWidget);
CmkWidget * page_home_new();

G_DECLARE_FINAL_TYPE(PageDriveSelect, page_drive_select, PAGE, DRIVE_SELECT, CmkWidget);
CmkWidget * page_drive_select_new();

G_DECLARE_FINAL_TYPE(PageProfile, page_profile, PAGE, PROFILE, CmkWidget);
CmkWidget * page_profile_new();

G_DECLARE_FINAL_TYPE(PageComplete, page_complete, PAGE, COMPLETE, CmkWidget);
CmkWidget * page_complete_new();

G_END_DECLS

#endif
