#include "XL3PacketTypes.h"
#include "XL3Registers.h"
#include "UnpackBundles.h"
#include "Globals.h"
#include "Json.h"

#include "DB.h"
#include "ControllerLink.h"
#include "XL3Model.h"
#include "MTCModel.h"
#include "XL3Cmds.h"
#include "SeeReflection.h"

#define RED "\x1b[31m"
#define RESET "\x1b[0m"

int SeeReflection(int crateNum, uint32_t slotMask, uint32_t channelMask, int dacValue, float frequency, int updateDB, int updateDetectorDB, int finalTest)
{
  lprintf("*** Starting See Reflection ************\n");
  lprintf("Warning, triggers will be enables for specified slots (one at a time)\n");

  char channel_results[32][100];

  try {

    // set up pulser
    int errors = mtc->SetupPedestals(frequency, DEFAULT_PED_WIDTH, DEFAULT_GT_DELAY,0,
        (0x1<<crateNum),(0x1<<crateNum) | MSK_TUB | MSK_TUB_B);
    if (errors){
      lprintf("Error setting up MTC for pedestals. Exiting\n");
      mtc->UnsetPedCrateMask(MASKALL);
      mtc->UnsetGTCrateMask(MASKALL);
      return -1;
    }

    // loop over slots
    for (int i=0;i<16;i++){
      if ((0x1<<i) & slotMask){

        // Enable triggers on this slot
        if( finalTest == 0)
          CrateInit(crateNum, slotMask,0,0,0,0,0,0,0,0,1);

        if(updateDetectorDB){
          lprintf(RED "Updating detectordb, 0 = Missing n100 and Missing n20, 1 = Missing N100, 2 = Missing N20.\n" RESET);
        }
        else{
          lprintf(RED "0 = Missing n100 and Missing n20, 1 = Missing N100, 2 = Missing N20, or just type in a custom message and hit enter.\n" RESET);
        }

        // loop over channels
        for (int j=0;j<32;j++){
          if ((0x1<<j) & channelMask){
            uint32_t temp_pattern = 0x1<<j;
            memset(channel_results[j],'\0',100);

            // turn on pedestals for just this one channel
            errors += xl3s[crateNum]->SetCratePedestals((0x1<<i),temp_pattern);
            if (errors){
              lprintf("Error setting up pedestals, Slot %d, channel %d.\n",i,j);
              if (errors > MAX_ERRORS){
                lprintf("Too many errors. Exiting\n");
                mtc->DisablePulser();
                mtc->UnsetPedCrateMask(MASKALL);
                mtc->UnsetGTCrateMask(MASKALL);
                return -1;
              }
            }

            // set up charge injection for this channel
            xl3s[crateNum]->SetupChargeInjection((0x1<<i),temp_pattern,dacValue);
            // wait until something is typed
            lprintf("Enabled triggers for: %d/%d/%d \n",crateNum,i,j);

            contConnection->GetInput(channel_results[j],100);

            for (int k=0;k<strlen(channel_results[j]);k++){
              if (channel_results[j][k] == '\n'){
                channel_results[j][k] = '\0';
              }
              else if(strncmp(channel_results[j],"0",1) == 0){
                strcpy(channel_results[j],"Missing N20 and N100.\n");
                if(updateDetectorDB)
                  UpdateTriggerStatus(0, crateNum, i, j);
              }
              else if(strncmp(channel_results[j],"1",1) == 0){
                strcpy(channel_results[j],"Missing N100.\n");
                if(updateDetectorDB)
                  UpdateTriggerStatus(1, crateNum, i, j);
              }
              else if(strncmp(channel_results[j],"2",1) == 0){
                strcpy(channel_results[j],"Missing N20.\n");
                if(updateDetectorDB)
                  UpdateTriggerStatus(2, crateNum, i, j);
              }
            }

            if (strncmp(channel_results[j],"quit",4) == 0){
              lprintf("Quitting.\n");
              mtc->DisablePulser();
              xl3s[crateNum]->DeselectFECs();
              return 0;
            }
          } // end pattern mask
        } // end loop over channels

        // clear chinj for this slot
        xl3s[crateNum]->SetupChargeInjection((0x1<<i),0x0,dacValue);

        // turn off triggers for this slot
        if( finalTest == 0)
          CrateInit(crateNum,slotMask,0,0,0,0,0,0,0,0,0);

        // update the database
        if (updateDB){
          lprintf("updating the database\n");
          lprintf("updating slot %d\n",i);
          JsonNode *newdoc = json_mkobject();
          json_append_member(newdoc,"type",json_mkstring("see_refl"));

          int passflag = 1;
          JsonNode *all_channels = json_mkarray();
          for (int j=0;j<32;j++){
            JsonNode *one_chan = json_mkobject();
            json_append_member(one_chan,"id",json_mknumber(j));
            if (strlen(channel_results[j]) != 0){
              passflag = 0;
              json_append_member(one_chan,"error",json_mkstring(channel_results[j]));
            }else{
              json_append_member(one_chan,"error",json_mkstring(""));
            }
            json_append_element(all_channels,one_chan);
          }
          json_append_member(newdoc,"channels",all_channels);
          json_append_member(newdoc,"pass",json_mkbool(passflag));
          if (finalTest)
            json_append_member(newdoc,"final_test_id",json_mkstring(finalTestIDs[crateNum][i]));	
          PostDebugDoc(crateNum,i,newdoc);
          json_delete(newdoc); // Only have to delete the head node
        }
      } // end if slot mask
    } // end loop over slots

    mtc->DisablePulser();
    xl3s[crateNum]->DeselectFECs();

    if (errors)
      lprintf("There were %d errors.\n",errors);
    else
      lprintf("No errors.\n");
  }
  catch(const char* s){
    lprintf("SeeReflection: %s\n",s);
  }

  lprintf("****************************************\n");
  return 0;
}
