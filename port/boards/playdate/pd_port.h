#ifndef PD_PORT_H
#define PD_PORT_H

#include <stddef.h>
#include "pd_api.h"

/* Set once in eventHandler(kEventInit) */
extern PlaydateAPI *qembd_pd;

/* Console line output (used by qembd_log) */
void pdq_log_line(const char *text);

/* Quake builds paths like ".//id1/pak0.pak"; the Playdate wants "id1/pak0.pak" */
const char *pdq_path(const char *path, char *out, size_t size);

/* Create every directory leading up to the file in path (in the Data folder) */
void pdq_mkdirs(const char *path);

/* The system menu or lock screen took over the device (true) or gave it back (false): music
 * pauses meanwhile (cd_pd.c) */
void qembd_cd_suspend(int suspend);

/* The system drew over the LCD frame buffer: redraw every row (display.c) */
void qembd_display_invalidate(void);

#endif /* PD_PORT_H */
