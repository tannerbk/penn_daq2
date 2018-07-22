#ifndef _ECAL_H
#define _ECAL_H

#include <unistd.h>

int ECAL(uint32_t crateMask, uint32_t *slotMasks, uint32_t testMask, int quickFlag, const char* loadECAL, int detectorFlag);

const int quick_test_mask = 0xF28;
const int num_ecal_tests = 12;
static const char testList[num_ecal_tests][30] = {"fec_test","board_id","cgt_test","crate_cbal","ped_run","set_ttot","get_ttot","disc_check","gtvalid_test","zdisc","find_noise","all_ped_by_channel"};

#endif

