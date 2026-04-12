#ifndef NSPIRE_FRAMESKIP_H
#define NSPIRE_FRAMESKIP_H

/* Wall-clock auto modes (RetroArch has audio buffer callbacks; we approximate). */
void nspire_frameskip_reset(void);
void nspire_frameskip_begin_frame(void);

#endif
