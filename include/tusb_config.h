#pragma once

#include_next "tusb_config.h"

#ifdef CFG_TUD_MSC_EP_BUFSIZE
#undef CFG_TUD_MSC_EP_BUFSIZE
#endif
#define CFG_TUD_MSC_EP_BUFSIZE 8192
