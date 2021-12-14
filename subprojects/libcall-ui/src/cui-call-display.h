/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Calls is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Calls is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Calls.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "cui-call.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CUI_TYPE_CALL_DISPLAY (cui_call_display_get_type ())

G_DECLARE_FINAL_TYPE (CuiCallDisplay, cui_call_display, CUI, CALL_DISPLAY, GtkOverlay);

CuiCallDisplay *cui_call_display_new        (CuiCall *call);
void            cui_call_display_set_call   (CuiCallDisplay *self,
                                             CuiCall        *call);
CuiCall        *cui_call_display_get_call   (CuiCallDisplay *self);

G_END_DECLS
