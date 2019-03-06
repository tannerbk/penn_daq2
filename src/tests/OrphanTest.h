#ifndef _ORPHAN_TEST_H
#define _ORPHAN_TEST_H

#include <unistd.h>

int OrphanTest(int crateNum, uint32_t slotMask, uint32_t channelMask, int updateDB, int finalTest=0, int ecal=0);

#endif

