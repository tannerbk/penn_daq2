#ifndef _GT_VALID_TEST_H
#define _GT_VALID_TEST_H

#include <unistd.h>

#define VMAX 203
#define TACREF 72
#define ISETM_MIN 110
#define ISETM_MAX_GTVALID 80 
#define ISETA 70
#define ISETA_NO_TWIDDLE 0

#define GTMAX 1000
#define GTMIN 250
#define GTPED_DELAY 20
#define TDELAY_EXTRA 0
#define NGTVALID 20
// GTValid should be shorter than lockout
#define LOCKOUT_WIDTH 420



void IsGTValidLonger(uint32_t crateMask, uint32_t *slotMasks, float time, uint16_t *islonger);
float MeasureGTValid(int crateNum, int slotNum, int tac, float max_gtvalid, uint32_t max_isetm);
int GTValidTest(uint32_t crateMask, uint32_t *slotMasks, uint32_t channelMask, float gtCutoff, int twiddleOn, int setOnly, int updateDB, int updateDetectorDB, int finalTest=0, int ecal=0);

#endif

