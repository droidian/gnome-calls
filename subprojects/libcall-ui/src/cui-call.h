/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define CUI_TYPE_CALL (cui_call_get_type ())
G_DECLARE_INTERFACE (CuiCall, cui_call, CUI, CALL, GObject)

/**
 * CuiCallState:
 *
 * The call state of a [iface@Cui.Call]
 */
typedef enum
{
  CUI_CALL_STATE_UNKNOWN = 0,
  CUI_CALL_STATE_ACTIVE,
  CUI_CALL_STATE_HELD,
  CUI_CALL_STATE_DIALING,
  CUI_CALL_STATE_ALERTING,
  CUI_CALL_STATE_INCOMING,
  CUI_CALL_STATE_WAITING,
  CUI_CALL_STATE_DISCONNECTED
} CuiCallState;

/**
 * CuiCallInterface:
 * @parent_iface: The parent interface
 * @get_avatar_icon: Get current calls's avatar icon
 * @get_display_name: Get current calls's display name
 * @get_id: Get current calls's id
 * @get_state: Get the call's state
 * @get_encrypted: Gets whether the call is encrypted
 * @get_can_dtmf: Gets whether the call can handle DTMF
 * @accept: Accept the incoming call
 * @hang_up: Hang-up an ongoing call or reject an incoming call
 * @send_dtmf: Send DTMF
 */
struct _CuiCallInterface {
  GTypeInterface parent_iface;

  GLoadableIcon *(*get_avatar_icon)        (CuiCall *self);
  const char    *(*get_display_name)       (CuiCall *self);
  const char    *(*get_id)                 (CuiCall *self);
  CuiCallState   (*get_state)              (CuiCall *self);
  gboolean       (*get_encrypted)          (CuiCall *self);
  gboolean       (*get_can_dtmf)           (CuiCall *self);

  void           (*accept)                 (CuiCall *self);
  void           (*hang_up)                (CuiCall *self);
  void           (*send_dtmf)              (CuiCall *self, const gchar *dtmf);
};

GLoadableIcon *cui_call_get_avatar_icon (CuiCall *self);
const char  *cui_call_get_display_name (CuiCall *self);
const char  *cui_call_get_id           (CuiCall *self);
CuiCallState cui_call_get_state        (CuiCall *self);
gboolean     cui_call_get_encrypted    (CuiCall *self);
gboolean     cui_call_get_can_dtmf     (CuiCall *self);

void         cui_call_accept           (CuiCall *self);
void         cui_call_hang_up          (CuiCall *self);
void         cui_call_send_dtmf (CuiCall *self, const gchar *dtmf);
