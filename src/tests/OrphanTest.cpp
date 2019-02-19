#include <stdlib.h>

#include "XL3PacketTypes.h"
#include "MTCPacketTypes.h"
#include "XL3Registers.h"
#include "MTCRegisters.h"
#include "Globals.h"
#include "Json.h"
#include "UnpackBundles.h"

#include "DB.h"
#include "MTCModel.h"
#include "XL3Model.h"
#include "OrphanTest.h"

int OrphanTest(int crateNum, uint32_t slotMask, uint32_t channelMask, int updateDB, int finalTest, int ecal)
{
  lprintf("*** Starting Orphan Test ******************\n");

  uint32_t result;

  uint32_t bundles[3];
  int crate_id,slot_id,chan_id,cell_id,gt16_id;

  int missing_bundles[16], chan_errors[16][32];
  char cur_msg[1000];
  uint32_t badchanmask;
  int num_chans;

  // zero some stuff
  memset(cur_msg,'\0',1000);

  num_chans = 0;
  for (int i=0;i<32;i++){
    if ((0x1<<i) & channelMask){
      num_chans++;
    }
  }

  for (int i=0;i<16;i++){
    for (int j=0;j<32;j++){
      chan_errors[i][j] = 0;
    }
    missing_bundles[i] = 0;
  }

  try{

    // set up mtc
    mtc->ResetMemory();
    //if (setup_pedestals(0,25,150,0,(0x1<<arg.crate_num)+MSK_TUB,(0x1<<arg.crate_num)+MSK_TUB))
    if (mtc->SetupPedestals(0,DEFAULT_PED_WIDTH,DEFAULT_GT_DELAY,DEFAULT_GT_FINE_DELAY,
          (0x1<<crateNum)+MSK_TUB,(0x1<<crateNum)+MSK_TUB)){
      lprintf("Error setting up mtc. Exiting\n");
      return -1;
    }
    mtc->SetGTCounter(0);

    // set up crate
    for (int i=0;i<16;i++){
      uint32_t select_reg = FEC_SEL*i;
      uint32_t crate_val = crateNum << FEC_CSR_CRATE_OFFSET;
      xl3s[crateNum]->RW(PED_ENABLE_R + select_reg + WRITE_REG,0x0,&result);
      xl3s[crateNum]->RW(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result);
      xl3s[crateNum]->RW(GENERAL_CSR_R + select_reg + WRITE_REG,crate_val | 0x6,&result);
      xl3s[crateNum]->RW(GENERAL_CSR_R + select_reg + WRITE_REG,crate_val,&result);
      xl3s[crateNum]->RW(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result);
    }
    xl3s[crateNum]->DeselectFECs();

    lprintf("Crate number: %d\n"
        "Slot and Channel mask: %08x %08x\n",crateNum,slotMask,channelMask);

    // select desired fecs
    if (xl3s[crateNum]->SetCratePedestals(slotMask,channelMask)){
      lprintf("Error setting up crate for pedestals. Exiting\n");
      return -1;
    }
    xl3s[crateNum]->DeselectFECs();

    uint32_t num_peds = 160; // test all cells 10 times
    lprintf("Going to fire pulser %u times.\n",num_peds);

    XL3Packet packet;
    memset(&packet, 0, sizeof(XL3Packet));
    int total_pulses = 0;
    int numgt = 1;
    // we now send out gts in bunches, checking periodically
    // that we are getting the right count at the fecs
    for (int j=0;j<num_peds;j++){

      mtc->MultiSoftGT(numgt);
      total_pulses += numgt;

      for (int i=0;i<16;i++){
        if (((0x1<<i) & slotMask)){
          uint32_t select_reg = FEC_SEL*i;
          xl3s[crateNum]->RW(FIFO_DIFF_PTR_R + select_reg + READ_REG,0x0,&result);
          if ((result & 0x000FFFFF) != 3*num_chans){
            sprintf(cur_msg,"Not enough bundles slot %d: expected %d, found %u\n",
                i,3*num_chans,result & 0x000FFFFF);
            lprintf("%s",cur_msg);
            missing_bundles[i] = 1;
          }

          // read out one bundle for each channel
          badchanmask = channelMask;
          for (int k=0;k<((result&0x000FFFFF)/3);k++){
            xl3s[crateNum]->RW(READ_MEM + select_reg,0x0,&bundles[0]);
            xl3s[crateNum]->RW(READ_MEM + select_reg,0x0,&bundles[1]);
            xl3s[crateNum]->RW(READ_MEM + select_reg,0x0,&bundles[2]);

            crate_id = (int) UNPK_CRATE_ID(bundles); 
            slot_id = (int) UNPK_BOARD_ID(bundles);
            chan_id = (int) UNPK_CHANNEL_ID(bundles);
            cell_id = (int) UNPK_CELL_ID(bundles);
            gt16_id = (int) UNPK_FEC_GT16_ID(bundles);

            // Debugging print to look at CELL ID
            //printf("%d %d %d\n", chan_id, cell_id, gt16_id);

            badchanmask &= ~(0x1<<chan_id);

            if (crate_id != crateNum){
              sprintf(cur_msg,"Crate wrong for slot %d, chan %u: expected %d, read %u\n",
                  i,chan_id,crateNum,crate_id);
              lprintf("%s",cur_msg);
              chan_errors[i][chan_id] = 1;
            } 
            if (slot_id != i){
              sprintf(cur_msg,"Slot wrong for slot %d chan %u: expected %d, read %u\n",
                  i,chan_id,i,slot_id);
              lprintf("%s",cur_msg);
              chan_errors[i][chan_id] = 1;
            } 
            if(gt16_id != total_pulses){
              sprintf(cur_msg,"Bad gtid for slot %d chan %u cell %d: expected %d, read %u\n"
                  "%08x %08x %08x\n",i,chan_id,cell_id,total_pulses,gt16_id,bundles[0],bundles[1],bundles[2]);
              lprintf("%s",cur_msg);
            }
          } // end loop over bundles being read out

          for (int k=0;k<32;k++){
            if ((0x1<<k) & badchanmask){
              sprintf(cur_msg,"No bundle found for slot %d chan %d\n",i,k);
              lprintf("%s",cur_msg);
              chan_errors[i][k] = 1;
            }
          }

        } // end if in slot mask and not max errors
      } // end loop over slots

      if(total_pulses%10==0){
        lprintf("%d pulses\n",total_pulses);
      }

    } // end loop over gt bunches

  }
  catch(const char* s){
    lprintf("OrphanTest: %s\n",s);
  }

  lprintf("Ending orphan test\n");
  lprintf("****************************************\n");
  return 0;
}


