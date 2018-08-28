#ifndef _TRIGGER_SCAN_H
#define _TRIGGER_SCAN_H

#include <unistd.h>

int TriggerScan(const uint32_t crateMask, uint32_t *slotMasks, const int
        triggerSelect, const int dacSelect, int maxNhit, const int
        minThresh, const char* fileName, const int quickMode, const int tub);

#endif

