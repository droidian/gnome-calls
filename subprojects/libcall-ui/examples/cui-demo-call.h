#pragma once

#include <cui-call.h>

G_BEGIN_DECLS

#define CUI_TYPE_DEMO_CALL (cui_demo_call_get_type())

G_DECLARE_FINAL_TYPE (CuiDemoCall, cui_demo_call, CUI, DEMO_CALL, GObject)

CuiDemoCall *cui_demo_call_new (void);

G_END_DECLS
