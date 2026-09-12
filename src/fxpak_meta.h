#ifndef FXPAK_META_H
#define FXPAK_META_H
#include <stdint.h>
#define FXMETA_MAILBOX 0xff5200UL
#define FXMETA_STAGING 0xff7200UL
#define FXMETA_RECORD_SIZE 256
#define FXMETA_VERSION 1
void fxmeta_init(void);
void fxmeta_cancel(void);
void fxmeta_poll(void);
#endif
