#ifndef _PED_RUN_BY_CHANNEL_H
#define _PED_RUN_BY_CHANNEL_H

#include <unistd.h>

int AllPedRunByChannel(int crateNum, uint32_t slotMask, uint32_t channelMask, float frequency, int gtDelay, int pedWidth, int numPedestals, int upper, int lower, int debugdb, int detectorDB, int ecal);

int PedRunByChannel(int crateNum, int slotNum, int channelNum, float frequency, int gtDelay, int pedWidth, int numPedestals, int upper, int lower, int updateDetectorDB);

double qhs_max;
double qhs_min;
double qhl_max;
double qhl_min;
double qlx_max;
double qlx_min;

#endif

