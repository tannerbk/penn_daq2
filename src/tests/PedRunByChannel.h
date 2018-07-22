#ifndef _PED_RUN_H
#define _PED_RUN_H

#include <unistd.h>

int PedRunByChannel(int crateNum, uint32_t slotNum, uint32_t channelNum, float frequency, int gtDelay, int pedWidth, int numPedestals, int updateDetectorDB);

#endif

