#include <stdlib.h>
#include "math.h"

#include "XL3PacketTypes.h"
#include "XL3Registers.h"
#include "UnpackBundles.h"
#include "Globals.h"
#include "Json.h"

#include "DB.h"
#include "XL3Model.h"
#include "XL3Cmds.h"
#include "MTCModel.h"
#include "PedRunByChannel.h"
#include "DetectorDB.h"

int AllPedRunByChannel(int crateNum, uint32_t slotMask, uint32_t channelMask, float frequency, int gtDelay, int pedWidth, int numPedestals, int upper, int lower, int updateDB, int detectorDB, int ecal=0){

  uint32_t error_flags[32];
  double qhsmax[32];
  double qlxmax[32];
  double qhlmax[32];
  double qhsmin[32];
  double qlxmin[32];
  double qhlmin[32];

  for(int slot = 0; slot < 16; slot++){
    if((1<<slot) & slotMask){
      for(int ch = 0; ch < 32; ch++){
        if((1<<ch) & channelMask){
          error_flags[ch] = PedRunByChannel(crateNum, slot, ch, frequency, gtDelay, pedWidth, numPedestals, upper, lower, detectorDB);
          qhsmax[ch] = qhs_max;
          qhlmax[ch] = qhl_max;
          qlxmax[ch] = qlx_max;
          qhsmin[ch] = qhs_min;
          qhlmin[ch] = qhl_min;
          qlxmin[ch] = qlx_min;
        }
      }
      if(updateDB){
        lprintf("updating the database for slot: %d\n", slot);
        JsonNode* newdoc = json_mkobject();
        JsonNode* errors = json_mkarray();
        JsonNode* qhlarray_max = json_mkarray();
        JsonNode* qhsarray_max = json_mkarray();
        JsonNode* qlxarray_max = json_mkarray();
        JsonNode* qhlarray_min = json_mkarray();
        JsonNode* qhsarray_min = json_mkarray();
        JsonNode* qlxarray_min = json_mkarray();
        int pass_flag = 1;
        for(int i = 0; i < 32; i++){
          if(error_flags[i] != 0){
            pass_flag = 0;
          }
          json_append_element(errors, json_mknumber(error_flags[i]));
          json_append_element(qhlarray_max, json_mknumber(qhlmax[i]));
          json_append_element(qhsarray_max, json_mknumber(qhsmax[i]));
          json_append_element(qlxarray_max, json_mknumber(qlxmax[i]));
          json_append_element(qhlarray_min, json_mknumber(qhlmin[i]));
          json_append_element(qhsarray_min, json_mknumber(qhsmin[i]));
          json_append_element(qlxarray_min, json_mknumber(qlxmin[i]));
        }
        json_append_member(newdoc,"type",json_mkstring("ped_run_by_channel"));
        json_append_member(newdoc,"errors",errors);
        json_append_member(newdoc,"qhs_max",qhsarray_max);
        json_append_member(newdoc,"qhl_max",qhlarray_max);
        json_append_member(newdoc,"qlx_max",qlxarray_max);
        json_append_member(newdoc,"qhs_min",qhsarray_min);
        json_append_member(newdoc,"qhl_min",qhlarray_min);
        json_append_member(newdoc,"qlx_min",qlxarray_min);
        json_append_member(newdoc,"pass",json_mkbool(pass_flag));
        if(ecal){
          json_append_member(newdoc,"ecal_id",json_mkstring(ecalID));
        }
        PostDebugDoc(crateNum,slot,newdoc);
        json_delete(newdoc); 
      }
    }
  }

  printf("Finished pedestaling.\n");
  return 0;
}

int PedRunByChannel(int crateNum, int slotNum, int channelNum, float frequency, int gtDelay, int pedWidth, int numPedestals, int upper, int lower, int updateDetectorDB)
{
  lprintf("*** Starting Pedestal Run By Channel **************\n");

  lprintf("-------------------------------------------\n");
  lprintf("Crate: %2d\n",crateNum);
  lprintf("Slot Num: %2d\n",slotNum);
  lprintf("Channel Num: %2d\n",channelNum);
  lprintf("GT delay (ns): %3hu\n", gtDelay);
  lprintf("Pedestal Width (ns): %2d\n",pedWidth);
  lprintf("Pulser Frequency (Hz): %2f\n",frequency);
  lprintf("Num pedestals: %d\n",numPedestals);

  uint32_t *pmt_buffer = (uint32_t *) malloc(0x100000*sizeof(uint32_t));
  if (pmt_buffer == (uint32_t *) NULL){
    lprintf("Problem mallocing!\n");
    return -1;
  }

  struct pedestal *ped = (struct pedestal *) malloc(32*sizeof(struct pedestal)); 
  if (ped == (struct pedestal *) NULL){
    lprintf("Problem mallocing!\n");
    free(pmt_buffer);
    return -1;
  }

  uint32_t error_flag = 0;

  try {

    // set up crate
    xl3s[crateNum]->ChangeMode(INIT_MODE,(1<<slotNum));

    // set up MTC
    int errors = mtc->SetupPedestals(frequency,pedWidth,gtDelay,DEFAULT_GT_FINE_DELAY,
        (0x1<<crateNum),(0x1<<crateNum));
    if (errors){
      lprintf("Error setting up MTC for pedestals. Exiting\n");
      mtc->UnsetPedCrateMask(MASKALL);
      mtc->UnsetGTCrateMask(MASKALL);
      free(pmt_buffer);
      free(ped);
      return -1;
    }

    uint32_t result;
    uint32_t slotMask = (1<<slotNum);
    uint32_t channelMask = (1<<channelNum);
    xl3s[crateNum]->RW(FIFO_DIFF_PTR_R + FEC_SEL*2 + READ_REG,0x0,&result);

    // reset the fifo
    XL3Packet packet;
    packet.header.packetType = RESET_FIFOS_ID;
    ResetFifosArgs *args = (ResetFifosArgs *) packet.payload;
    args->slotMask = slotMask;
    SwapLongBlock(args,sizeof(ResetFifosArgs)/sizeof(uint32_t));
    xl3s[crateNum]->SendCommand(&packet);

    xl3s[crateNum]->RW(FIFO_DIFF_PTR_R + FEC_SEL*2 + READ_REG,0x0,&result);

    xl3s[crateNum]->DeselectFECs();
    errors = xl3s[crateNum]->LoadCrateAddr(slotMask);

    xl3s[crateNum]->RW(FIFO_DIFF_PTR_R + FEC_SEL*2 + READ_REG,0x0,&result);

    errors += xl3s[crateNum]->SetCratePedestals(slotMask,channelMask);
    xl3s[crateNum]->RW(FIFO_DIFF_PTR_R + FEC_SEL*2 + READ_REG,0x0,&result);

    xl3s[crateNum]->DeselectFECs();
    if (errors){
      lprintf("Error setting up crate for pedestals. Exiting\n");
      free(pmt_buffer);
      free(ped);
      return -1;
    }
    // send pedestals
    uint32_t totalPulses = numPedestals*16;
    uint32_t beforeGT, afterGT;
    mtc->RegRead(MTCOcGtReg,&beforeGT);

    if (frequency == 0){
      int num_to_send = numPedestals*16;
      while (num_to_send > 0){
        if (num_to_send > 1000){
          mtc->MultiSoftGT(1000);
          num_to_send-=1000;
        }else{
          mtc->MultiSoftGT(num_to_send);
          num_to_send = 0;
        }
      }
      mtc->DisablePulser();
    }
    else{
      float wait_time = (float) numPedestals*16.0/frequency*1E6;
      usleep((int) wait_time);
      mtc->DisablePulser();
    }

    mtc->RegRead(MTCOcGtReg,&afterGT);
    totalPulses = (afterGT - beforeGT);
    printf("Total pulses: %d (%d bundles)\n",totalPulses,totalPulses);
    xl3s[crateNum]->RW(FIFO_DIFF_PTR_R + FEC_SEL*2 + READ_REG,0x0,&result);

    errors = 0;

    // initialize pedestal struct
    ped[channelNum].channelnumber = channelNum;
    ped[channelNum].per_channel = 0;
    for (int j=0;j<16;j++){
      ped[channelNum].thiscell[j].cellno = j;
      ped[channelNum].thiscell[j].per_cell = 0;
      ped[channelNum].thiscell[j].qlxbar = 0;
      ped[channelNum].thiscell[j].qlxrms = 0;
      ped[channelNum].thiscell[j].qhlbar = 0;
      ped[channelNum].thiscell[j].qhlrms = 0;
      ped[channelNum].thiscell[j].qhsbar = 0;
      ped[channelNum].thiscell[j].qhsrms = 0;
      ped[channelNum].thiscell[j].tacbar = 0;
      ped[channelNum].thiscell[j].tacrms = 0;
    }
    
    // readout bundles
    int count = xl3s[crateNum]->ReadOutBundles(slotNum,pmt_buffer,totalPulses,0);

    if (count <= 0){
      lprintf("There was an error in the count!\n");
      lprintf("Errors reading out MB %2d (errno %d)\n",slotNum,count);
      errors++;
    }

    lprintf("MB %d: %d bundles read out.\n",slotNum,count);
    if (count < totalPulses){
      errors++;
    }

    // process data
    uint32_t *pmt_iter = pmt_buffer;
    int ch,cell,crateID,num_events;
    std::vector< std::vector<int> > pedQHSByCell(16, std::vector <int> (0));

    for (int i=0;i<count;i++){
      crateID = (int) UNPK_CRATE_ID(pmt_iter);
      if (crateID != crateNum){
        lprintf( "Invalid crate ID seen! (crate ID %2d, bundle %2i)\n",crateID,i);
        pmt_iter+=3;
        continue;
      }
      ch = (int) UNPK_CHANNEL_ID(pmt_iter);
      cell = (int) UNPK_CELL_ID(pmt_iter);

      int qhs = (int) UNPK_QHS(pmt_iter); 
      pedQHSByCell[cell].push_back(qhs);

      ped[ch].thiscell[cell].qlxbar += (double) MY_UNPK_QLX(pmt_iter);
      ped[ch].thiscell[cell].qhsbar += (double) UNPK_QHS(pmt_iter);
      ped[ch].thiscell[cell].qhlbar += (double) UNPK_QHL(pmt_iter);
      ped[ch].thiscell[cell].tacbar += (double) UNPK_TAC(pmt_iter);

      ped[ch].thiscell[cell].qlxrms += pow((double) MY_UNPK_QLX(pmt_iter),2.0);
      ped[ch].thiscell[cell].qhsrms += pow((double) UNPK_QHS(pmt_iter),2.0);
      ped[ch].thiscell[cell].qhlrms += pow((double) UNPK_QHL(pmt_iter),2.0);
      ped[ch].thiscell[cell].tacrms += pow((double) UNPK_TAC(pmt_iter),2.0);

      ped[ch].per_channel++;
      ped[ch].thiscell[cell].per_cell++;

      pmt_iter += 3; //increment pointer
    }


    // do final step
    // final step of calculation
    if (ped[channelNum].per_channel > 0){
      for (int j=0;j<16;j++){
        num_events = ped[channelNum].thiscell[j].per_cell;

        //don't do anything if there is no data here or n=1 since
        //that gives 1/0 below.
        if (num_events > 1){

          // now x_avg = sum(x) / N, so now xxx_bar is calculated
          ped[channelNum].thiscell[j].qlxbar /= num_events;
          ped[channelNum].thiscell[j].qhsbar /= num_events;
          ped[channelNum].thiscell[j].qhlbar /= num_events;
          ped[channelNum].thiscell[j].tacbar /= num_events;

          // now x_rms^2 = n/(n-1) * (<xxx^2>*N/N - xxx_bar^2)
          ped[channelNum].thiscell[j].qlxrms = num_events / (num_events -1)
            * ( ped[channelNum].thiscell[j].qlxrms / num_events
                - pow( ped[channelNum].thiscell[j].qlxbar, 2.0));
          ped[channelNum].thiscell[j].qhlrms = num_events / (num_events -1)
            * ( ped[channelNum].thiscell[j].qhlrms / num_events
                - pow( ped[channelNum].thiscell[j].qhlbar, 2.0));
          ped[channelNum].thiscell[j].qhsrms = num_events / (num_events -1)
            * ( ped[channelNum].thiscell[j].qhsrms / num_events
                - pow( ped[channelNum].thiscell[j].qhsbar, 2.0));
          ped[channelNum].thiscell[j].tacrms = num_events / (num_events -1)
            * ( ped[channelNum].thiscell[j].tacrms / num_events
                - pow( ped[channelNum].thiscell[j].tacbar, 2.0));

          // finally x_rms = sqrt(x_rms^2)
          if (ped[channelNum].thiscell[j].qlxrms > 0 || ped[channelNum].thiscell[j].qlxrms == 0)
            ped[channelNum].thiscell[j].qlxrms = sqrt(ped[channelNum].thiscell[j].qlxrms);
          else
            ped[channelNum].thiscell[j].qlxrms = -1;
          if (ped[channelNum].thiscell[j].qhsrms > 0 || ped[channelNum].thiscell[j].qhsrms == 0)
            ped[channelNum].thiscell[j].qhsrms = sqrt(ped[channelNum].thiscell[j].qhsrms);
          else
            ped[channelNum].thiscell[j].qhsrms = -1;
          if (ped[channelNum].thiscell[j].qhlrms > 0 || ped[channelNum].thiscell[j].qhlrms == 0)
            ped[channelNum].thiscell[j].qhlrms = sqrt(ped[channelNum].thiscell[j].qhlrms);
          else
            ped[channelNum].thiscell[j].qhlrms = -1;
          if (ped[channelNum].thiscell[j].tacrms > 0 || ped[channelNum].thiscell[j].tacrms == 0)
            ped[channelNum].thiscell[j].tacrms = sqrt(ped[channelNum].thiscell[j].tacrms);
          else
            ped[channelNum].thiscell[j].tacrms = -1;
        }
        else{
          ped[channelNum].thiscell[j].qlxrms = 0;
          ped[channelNum].thiscell[j].qhsrms = 0;
          ped[channelNum].thiscell[j].qhlrms = 0;
          ped[channelNum].thiscell[j].tacrms = 0;
        }
      }
    }

    qhs_max = 0.0, qhl_max = 0.0, qlx_max = 0.0;
    qhs_min = 4095.0, qhl_min = 4095.0, qlx_min = 4095.0;

    for (int j=0;j<16;j++){

      if(ped[channelNum].thiscell[j].qhlbar > qhl_max){
        qhl_max = ped[channelNum].thiscell[j].qhlbar;
      }
      if(ped[channelNum].thiscell[j].qhsbar > qhs_max){
        qhs_max = ped[channelNum].thiscell[j].qhsbar;
      }
      if(ped[channelNum].thiscell[j].qlxbar > qlx_max){
        qlx_max = ped[channelNum].thiscell[j].qlxbar;
      }

      if(ped[channelNum].thiscell[j].qhlbar < qhl_min){
        qhl_min = ped[channelNum].thiscell[j].qhlbar;
      }
      if(ped[channelNum].thiscell[j].qhsbar < qhs_min){
        qhs_min = ped[channelNum].thiscell[j].qhsbar;
      }
      if(ped[channelNum].thiscell[j].qlxbar < qlx_min){
        qlx_min = ped[channelNum].thiscell[j].qlxbar;
      }

      if (ped[channelNum].thiscell[j].per_cell < totalPulses/16*.8 || 
          ped[channelNum].thiscell[j].per_cell > totalPulses/16*1.2){
        error_flag |= 0x1;
      }
      if (ped[channelNum].thiscell[j].qhlbar < lower ||
          ped[channelNum].thiscell[j].qhlbar > upper ||
          ped[channelNum].thiscell[j].qhsbar < lower ||
          ped[channelNum].thiscell[j].qhsbar > upper ||
          ped[channelNum].thiscell[j].qlxbar < lower ||
          ped[channelNum].thiscell[j].qlxbar > upper){
        error_flag |= 0x2;
      }
      if (ped[channelNum].thiscell[j].qhlrms > 48.0 ||
          ped[channelNum].thiscell[j].qhsrms > 48.0 ||
          ped[channelNum].thiscell[j].qlxrms > 48.0){
        error_flag |= 0x4;
       }
     }


    lprintf("Ch Cell  #   Qhl         Qhs         Qlx         TAC\n");
    for (int j=0;j<16;j++){
      lprintf("%2d %3d %4d %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f %6.1f %4.1f\n",
          channelNum,j,ped[channelNum].thiscell[j].per_cell,
          ped[channelNum].thiscell[j].qhlbar, ped[channelNum].thiscell[j].qhlrms,
          ped[channelNum].thiscell[j].qhsbar, ped[channelNum].thiscell[j].qhsrms,
          ped[channelNum].thiscell[j].qlxbar, ped[channelNum].thiscell[j].qlxrms,
          ped[channelNum].thiscell[j].tacbar, ped[channelNum].thiscell[j].tacrms);
    }

    // disable triggers
    mtc->UnsetPedCrateMask(MASKALL);
    mtc->UnsetGTCrateMask(MASKALL);

    // turn off pedestals
    xl3s[crateNum]->SetCratePedestals(slotMask,0x0);
    xl3s[crateNum]->DeselectFECs();
    if (errors){
      lprintf("There were %d errors\n",errors);
    }
    else{
      lprintf("No run errors seen\n");
    }

    if(updateDetectorDB){
      if(numPedestals <= 1000){
        PGconn* detectorDB = ConnectToDetectorDB();
        for(int cell = 0; cell < 16; cell++){
          if(LoadPedestalsToDetectorDB(crateNum, slotNum, channelNum, cell, pedQHSByCell[cell], 
             ped[channelNum].thiscell[cell].qhsbar, ped[channelNum].thiscell[cell].qhsrms,
             ped[channelNum].thiscell[cell].qhlbar, ped[channelNum].thiscell[cell].qhlrms,
             ped[channelNum].thiscell[cell].qlxbar, ped[channelNum].thiscell[cell].qlxrms,
             detectorDB)){
            lprintf("Failure loading pedestals to detectorDB for "
                    "crate %d, slot %d, ch %d, cell %d \n", crateNum, slotNum, channelNum, cell);
          }
        }
        CloseDetectorDBConnection(detectorDB);
      }
      else{
        printf("Choose fewer pedestals to upload to detectorDB\n");
      }
    }

  }
  catch(const char* s){
    lprintf("PedRunByChannel: %s\n",s);
  }
  free(pmt_buffer);
  free(ped);
  lprintf("****************************************\n");

  return error_flag;
}

