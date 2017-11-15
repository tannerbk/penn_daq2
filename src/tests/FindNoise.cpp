#include <stdlib.h>
#include "XL3PacketTypes.h"
#include "XL3Registers.h"
#include "DacNumber.h"
#include "Globals.h"
#include "Json.h"
#include "Pouch.h"

#include "DB.h"
#include "XL3Model.h"
#include "MTCModel.h"
#include "FindNoise.h"


int FindNoise(uint32_t crateMask, uint32_t *slotMasks, float frequency, int useDebug, int channelTuning, int plusOne, int updateDB, int ecal)
{
  lprintf("*** Starting Noise Run *****************\n");
  lprintf("All crates and mtcs should have been inited with proper values already\n");

  uint32_t total_count1[8][32];
  uint32_t total_count2[8][32];
  uint32_t slot_nums[50];
  uint32_t dac_nums[50];
  uint32_t dac_values[50];

  uint32_t *vthr_zeros = (uint32_t *) malloc(sizeof(uint32_t) * 10300);
  uint32_t *current_vthr = (uint32_t *) malloc(sizeof(uint32_t) * 10300);
  uint32_t *current_vthr2 = (uint32_t *) malloc(sizeof(uint32_t) * 10300);

  // malloc some room to store the total counts in between measurements
  uint32_t *readout_noise = (uint32_t *) malloc(sizeof(uint32_t) * 10300);

  char get_db_address[500];

  try {

  if (useDebug){
    // use zdisc debug values
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        xl3s[i]->UpdateCrateConfig(slotMasks[i]);
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            char configString[500];
            sprintf(configString,"\"%04x\",\"%04x\",\"%04x\",\"%04x\",\"%04x\"",
                xl3s[i]->GetMBID(j),xl3s[i]->GetDBID(j,0),
                xl3s[i]->GetDBID(j,1),xl3s[i]->GetDBID(j,2),
                xl3s[i]->GetDBID(j,3));
            sprintf(get_db_address,"%s/%s/%s/get_zdisc?startkey=[%s,9999999999]&endkey=[%s,0]&descending=true",
                DB_SERVER,DB_BASE_NAME,DB_VIEWDOC,configString,configString);
            pouch_request *zdisc_response = pr_init();
            pr_set_method(zdisc_response, GET);
            pr_set_url(zdisc_response, get_db_address);
            pr_do(zdisc_response);
            if (zdisc_response->httpresponse != 200){
              lprintf("Unable to connect to database. error code %d\n",(int)zdisc_response->httpresponse);
              free(vthr_zeros);
              free(current_vthr);
              free(current_vthr2);
              free(readout_noise);
              return -1;
            }
            JsonNode *viewdoc = json_decode(zdisc_response->resp.data);
            JsonNode *viewrows = json_find_member(viewdoc,"rows");
            int n = json_get_num_mems(viewrows);
            if (n == 0){
              lprintf("Crate %d Slot %d: No zdisc documents for this configuration (%s). Exiting\n",i,j,configString);
              free(vthr_zeros);
              free(current_vthr);
              free(current_vthr2);
              free(readout_noise);
              return -1;
            }
            JsonNode *zdisc_doc = json_find_member(json_find_element(viewrows,0),"value");
            JsonNode *zero_dac = json_find_member(zdisc_doc,"zero_dac");
            for (int k=0;k<32;k++){
              vthr_zeros[i*32*16+j*32+k] = (uint32_t) json_get_number(json_find_element(zero_dac,k));
            }
            json_delete(viewdoc);
            pr_free(zdisc_response);
          }
        }
      }
    }
  }else{
    // use the ECAL values
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            sprintf(get_db_address,"%s/%s/%s/get_fec?startkey=[%d,%d,\"\"]&endkey=[%d,%d]&descending=true",
                FECDB_SERVER,FECDB_BASE_NAME,FECDB_VIEWDOC,i,j+1,i,j);
            pouch_request *zdisc_response = pr_init();
            pr_set_method(zdisc_response, GET);
            pr_set_url(zdisc_response, get_db_address);
            pr_do(zdisc_response);
            if (zdisc_response->httpresponse != 200){
              lprintf("Unable to connect to database. error code %d\n",(int)zdisc_response->httpresponse);
              free(vthr_zeros);
              free(current_vthr);
              free(current_vthr2);
              free(readout_noise);
              return -1;
            }
            JsonNode *viewdoc = json_decode(zdisc_response->resp.data);
            JsonNode *viewrows = json_find_member(viewdoc,"rows");
            int n = json_get_num_mems(viewrows);
            if (n == 0){
              lprintf("Crate %d Slot %d: No FEC document. Exiting\n",i,j);
              free(vthr_zeros);
              free(current_vthr);
              free(current_vthr2);
              free(readout_noise);
              return -1;
            }
            JsonNode *zdisc_doc = json_find_member(json_find_element(viewrows,0),"value");
            JsonNode *hw = json_find_member(zdisc_doc,"hw");
            JsonNode *zero_dac = json_find_member(zdisc_doc,"zero_dac");
            for (int k=0;k<32;k++){
              vthr_zeros[i*32*16+j*32+k] = (uint32_t) json_get_number(json_find_element(zero_dac,k));
            }
            json_delete(viewdoc);
            pr_free(zdisc_response);
          }
        }
      }
    }
  } // finished getting vthr_zero values from database

  // set up mtcd for pulsing continuously
  if (mtc->SetupPedestals(frequency,DEFAULT_PED_WIDTH,DEFAULT_GT_DELAY,DEFAULT_GT_FINE_DELAY,
        crateMask, crateMask | MSK_TUB | MSK_TUB_B)){
    lprintf("Error setting up MTC. Exiting\n");
    free(vthr_zeros);
    free(current_vthr);
    free(current_vthr2);
    free(readout_noise);
    return -1;
  }

  // set all vthr dacs to 255 in all crates in all slots
  for (int i=0;i<MAX_XL3_CON;i++){
    if ((0x1<<i) & crateMask){
      for (int j=0;j<16;j++){
        if ((0x1<<j) & slotMasks[i]){
          for (int k=0;k<32;k++){
            slot_nums[k] = j;
            dac_nums[k] = d_vthr[k];
            dac_values[k] = 255;
          }
          if (xl3s[i]->MultiLoadsDac(32,dac_nums,dac_values,slot_nums)){
            lprintf("Error loading dacs. Exiting\n");
            free(vthr_zeros);
            free(current_vthr);
            free(current_vthr2);
            free(readout_noise);
            return -1;
          }
        }
      }
    }
  }

  // enable all slots pedestals
  for (int i=0;i<MAX_XL3_CON;i++)
    if ((0x1<<i) & crateMask)
      xl3s[i]->SetCratePedestals(0xFFFF,0xFFFFFFFF);

  // enable readout on all crates
  for (int i=0;i<MAX_XL3_CON;i++)
    if ((0x1<<i) & crateMask)
      xl3s[i]->ChangeMode(NORMAL_MODE,slotMasks[i]);

  // now we have pedestals going at $(frequency) hz, and all channels in all
  // slots in all crates are reading out. We will start each channels threshold
  // at 50 counts above zero, and step each channel down until it starts firing
  // faster than the pulser. At that point we will step it back up one and
  // continue with all other channels. Once we have all channels reset and all
  // still firing at the same rate as the pulser, we will step down each
  // channel one at a time, to see if without the others going crazy we can
  // lower it further. 
  // set up masks of which channels we've found the right threshold

  uint32_t found_above[MAX_XL3_CON][16];
  uint32_t found_below[MAX_XL3_CON][16];
  uint32_t done_channel[MAX_XL3_CON][16];
  uint32_t error_channel[MAX_XL3_CON][16];


  // set up variables, going to start all at zero+25
  for (int i=0;i<MAX_XL3_CON;i++)
    for (int j=0;j<16;j++){
      error_channel[i][j] = 0x0;
      done_channel[i][j] = 0x0;
      found_above[i][j] = 0x0;
      found_below[i][j] = 0x0;
      for (int k=0;k<32;k++){
        if (vthr_zeros[i*16*32+j*32+k] >= 255){
          error_channel[i][j] |= 0x1<<k;
          done_channel[i][j] |= 0x1<<k;
          current_vthr[i*16*32+j*32+k] = 255;
        }else{
          current_vthr[i*16*32+j*32+k] = vthr_zeros[i*16*32+j*32+k]+25;
        }
        current_vthr2[i*16*32+j*32+k] = 0;
      }
    }

  int done = 0;
  int iterations = 0;
  int printiter = 0;

  // loop until all channels done
  while (done == 0){
    // load new thresholds
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        if(printiter == 0){
          printiter++;
          printf("Finding discriminator thresholds for crate %d.\n", i);
        }
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            int numDacs = 0;
            for (int k=0;k<32;k++){
              slot_nums[numDacs] = j;
              dac_nums[numDacs] = d_vthr[k];
              dac_values[numDacs] = current_vthr[i*16*32+j*32+k];
              numDacs++;
            }
            if (xl3s[i]->MultiLoadsDac(numDacs,dac_nums,dac_values,slot_nums)){
              lprintf("Error loading dacs. Exiting\n");
              free(readout_noise);
              free(vthr_zeros);
              free(current_vthr);
              free(current_vthr2);

              for (int c=0;c<MAX_XL3_CON;c++)
                if ((0x1<<c) & crateMask)
                  xl3s[c]->ChangeMode(INIT_MODE,slotMasks[c]);
              return -1;
            }
          }
        }
      }
    } // end loading thresholds

    // now that we have our new thresholds, we will measure total count, wait,
    // then measure total count again
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF,total_count1);
        xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF00,total_count2);
        int slotIter = 0;
        for (int j=0;j<8;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++)
              readout_noise[i*16*32+j*32+k] = total_count1[slotIter][k];
            slotIter++;
          }
        }
        slotIter = 0;
        for (int j=8;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++)
              readout_noise[i*16*32+j*32+k] = total_count2[slotIter][k];
            slotIter++;
          }
        }
      }
    }
    // done getting number of counts first time

    // now we wait
    for (int i=0;i<2;i++)
      usleep(SLEEP_TIME);

    // measure total count again
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF,total_count1);
        xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF00,total_count2);
        int slotIter = 0;
        for (int j=0;j<8;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++){
              if (readout_noise[i*16*32+j*32+k] > total_count1[slotIter][k])
                readout_noise[i*16*32+j*32+k] = 0xFFFFFFFF;
              else
                readout_noise[i*16*32+j*32+k] = total_count1[slotIter][k] - readout_noise[i*16*32+j*32+k];
            }
            slotIter++;
          }
        }
        slotIter = 0;
        for (int j=8;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++){
              if (readout_noise[i*16*32+j*32+k] > total_count2[slotIter][k])
                readout_noise[i*16*32+j*32+k] = 0xFFFFFFFF;
              else
                readout_noise[i*16*32+j*32+k] = total_count2[slotIter][k] - readout_noise[i*16*32+j*32+k];
            }
            slotIter++;
          }
        }
      }
    }
    // done getting number of counts second time

    // now we check each channel and see how we should adjust each threshold
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++){
              int doneornot = 0;
              int errorornot = 0;
              if (done_channel[i][j] & 0x1<<k)
                doneornot = 1;
              if (error_channel[i][j] & 0x1<<k)
                errorornot = 1;
              //printf("%2d %2d %2d (%1d, %1d): %3d, %7u (%.0f) ",i,j,k,doneornot,errorornot,current_vthr[i*16*32+j*32+k],readout_noise[i*16*32+j*32+k],frequency*(2*SLEEP_TIME/1e6+0.025)+1);
              if (!((0x1<<k) & error_channel[i][j])){
                if (!((0x1<<k) & done_channel[i][j])){
                  // check if there were more hits than what we put in (plus 1 second extra fudge factor for now)
                  if (readout_noise[i*16*32+j*32+k] > frequency*(2*SLEEP_TIME/1e6+0.025)+1){
                    // mark that we've been below the noise
                    found_below[i][j] |= 0x1<<k;
                    // we've found the point where we start getting noise. step back up one
                    current_vthr[i*16*32+j*32+k]++;
                    if (current_vthr[i*16*32+j*32+k] >= 255){
                      current_vthr[i*16*32+j*32+k] = 255;
                      error_channel[i][j] |= 0x1<<k;
                    }
                    // check if we've been above the noise yet
                    //if (found_above[i][j] & ((0x1<<k)))
                      //printf("++ ready\n");
                    //else
                      // we are still in mid noise, so bump it up, but dont call it done
                      //printf("++\n");
                  }else{
                    // mark that we've been above the noise
                    found_above[i][j] |= 0x1<<k;
                    // if we've been below the noise also, we should now be one
                    // count above it and can call it done
                    if (found_below[i][j] & ((0x1<<k))){
                      done_channel[i][j] |= 0x1<<k;
                      //printf("done\n");
                    }else{
                      // still not low enough, go down one
                      current_vthr[i*16*32+j*32+k]--;
                      if (current_vthr[i*16*32+j*32+k] <= 0){
                        current_vthr[i*16*32+j*32+k] = 0;
                        error_channel[i][j] |= 0x1<<k;
                      }
                      //printf("--\n");
                    }
                  }
                }else{
                  // since we're already done, shouldn't have noise now, so double check
                  if (readout_noise[i*16*32+j*32+k] > frequency*(2*SLEEP_TIME/1e6+0.025)+1){
                    // take it out of done mask
                    done_channel[i][j] &= ~(0x1<<k);
                    // step back up one
                    current_vthr[i*16*32+j*32+k]++;
                    if (current_vthr[i*16*32+j*32+k] >= 255){
                      current_vthr[i*16*32+j*32+k] = 255;
                      error_channel[i][j] |= 0x1<<k;
                    }
                    // if already max iterations, only step back up. otherwise, can step up and down
                    found_above[i][j] &= ~(0x1<<k);
                    if (iterations < MAX_ITERATIONS){
                      found_below[i][j] &= ~(0x1<<k);
                      //printf("++ undone\n");
                    }//else{
                    //  printf("++ undone (no--)\n");
                    //}
                  }//else{
                  //  printf("\n");
                  //}
                }
              }//else{
              //  printf("\n");
              //}
            }
          }
        }
      }
    }

    done = 1;
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            if ((done_channel[i][j] | error_channel[i][j]) != 0xFFFFFFFF){
              done = 0;
            }
          }
        }
      }
    }

    iterations++;
  } // end while(done == 0)

  // set vthr2 equal to vthr, this will allow us to compare the thresholds
  // set by each method at the end. The first method has the disadvantage that
  // it is sensitive to a couple noisy channels making everything bad. The second
  // method has the disadvantage that it sets the discriminator thresholds too low
  // and a manual +1 to all DAC counts is added to account for this
  for (int i=0;i<MAX_XL3_CON;i++)
    if ((0x1<<i) & crateMask)
      for (int j=0;j<16;j++)
        if ((0x1<<j) & slotMasks[i])
          for (int k=0;k<32;k++)
            current_vthr2[i*16*32+j*32+k] = current_vthr[i*16*32+j*32+k];

  // we should now have every channel with no extra noise. Now lower each
  // channel one by one, make sure it wasn't some other noisy channel making it
  // look noisy.
  if (channelTuning){
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        printf("Adjusting discriminator thresholds for crate %d.\n", i);
        int slotIter = 0;
        int slotIter2 = 0;
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++){

              int steps = 0;
              int done = 0;
              while (true){

                slot_nums[k] = j;
                dac_nums[k] = d_vthr[k];
                dac_values[k] = current_vthr2[i*16*32+j*32+k];

                if (xl3s[i]->MultiLoadsDac(32,dac_nums,dac_values,slot_nums)){
                  lprintf("Error loading dacs. Exiting\n");
                  free(readout_noise);
                  free(vthr_zeros);
                  free(current_vthr);
                  free(current_vthr2);

                  for (int c=0;c<MAX_XL3_CON;c++)
                    if ((0x1<<c) & crateMask)
                      xl3s[c]->ChangeMode(INIT_MODE,slotMasks[c]);
                  return -1;
                }

                // CMOS rate polling is done on bottom and top 8 slot seperately,
                // thus the two slotIter counters and this if/else condition
                if(j < 8){
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter][k];
                }
                else{ 
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF00, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter2][k];
                }

                // now we wait
                for (int t=0;t<2;t++)
                  usleep(SLEEP_TIME);

                if(j < 8){ 
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter][k] - readout_noise[i*16*32+j*32+k];
                }
                else{ 
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF00, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter2][k] - readout_noise[i*16*32+j*32+k];
                }

                //printf("%2d %2d %2d: %3d, %7u (%.0f) \n",i,j,k,current_vthr2[i*16*32+j*32+k],readout_noise[i*16*32+j*32+k],frequency*(2*SLEEP_TIME/1e6+0.025)+1);

                // First check for issues, if there is one, max the threshold
                if (current_vthr2[i*16*32+j*32+k] <= vthr_zeros[i*16*32+j*32+k] 
                    || current_vthr2[i*16*32+j*32+k] == 255
                    || current_vthr2[i*16*32+j*32+k] == 0){
                  current_vthr2[i*16*32+j*32+k] = 255;
                  //printf("Done Error: %2d %2d %2d: %3d \n",i,j,k,current_vthr2[i*16*32+j*32+k]);
                  break;
                }
                // Look for thresholds at or below 5 counts, set hard limit there
                else if((current_vthr2[i*16*32+j*32+k] - vthr_zeros[i*16*32+j*32+k]) < 6){
                  if(steps == 0){
                    current_vthr2[i*16*32+j*32+k] = vthr_zeros[i*16*32+j*32+k] + 5;
                  }
                  else if (readout_noise[i*16*32+j*32+k] < frequency*(2*SLEEP_TIME/1e6+0.025)+1){
                    current_vthr2[i*16*32+j*32+k] = vthr_zeros[i*16*32+j*32+k] + 5;
                  }
                  else{
                    current_vthr2[i*16*32+j*32+k] = vthr_zeros[i*16*32+j*32+k] + 6;
                  }
                  //printf("Done Min Thresh: %2d %2d %2d: %3d \n",i,j,k,current_vthr2[i*16*32+j*32+k]);
                  break;
                }
                // Look for channels that are too noisy now, try and step them up 1 count
                if (readout_noise[i*16*32+j*32+k] > 10*frequency*(2*SLEEP_TIME/1e6+0.025)+1){
                  current_vthr2[i*16*32+j*32+k]++;
                  break;
                }
                // Channel is noisy, if its not the first step, step the threshold back up
                if (readout_noise[i*16*32+j*32+k] > frequency*(2*SLEEP_TIME/1e6+0.025)+1){
                  if(steps > 0){
                    current_vthr2[i*16*32+j*32+k]++;
                  }
                  //printf("Done: %2d %2d %2d: %3d \n",i,j,k,current_vthr2[i*16*32+j*32+k]);
                  break;
                }
                // Don't step any threshold down more then 3 times, otherwise they get too noisy
                else if (steps == 3){
                  //printf("Done: %2d %2d %2d: %3d \n",i,j,k,current_vthr2[i*16*32+j*32+k]);
                  break;
                }
                // Channel is quiet, step the threshold down and try again
                else{
                  current_vthr2[i*16*32+j*32+k]--;
                  steps++;
                }
              } // end while loop
            }
            if(j < 8)
              slotIter++;
            else
              slotIter2++;
          }
        }
      }
    }

    // Disable Peds, now lets go look for noisy channels
    mtc->DisablePulser();

    // Noisy channels are identified as channels firing at more 
    // than 100Hz at low voltage with no pedestals going. The 
    // discriminator thresholds are stepped up a maximum of 5
    // DAC counts before giving up
    for (int i=0;i<MAX_XL3_CON;i++){
     if ((0x1<<i) & crateMask){
        int slotIter = 0;
        int slotIter2 = 0;
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            for (int k=0;k<32;k++){
              int count_noisy_channels = 0;
              while(true){
                int steps = 0;
                int done = 0;

                // Channel is definitely broken, so just give up
                if(current_vthr2[i*16*32+j*32+k] > 250){
                   break;
                }

                slot_nums[k] = j;
                dac_nums[k] = d_vthr[k];
                dac_values[k] = current_vthr2[i*16*32+j*32+k];

                if (xl3s[i]->MultiLoadsDac(32,dac_nums,dac_values,slot_nums)){
                  lprintf("Error loading dacs. Exiting\n");
                  free(readout_noise);
                  free(vthr_zeros);
                  free(current_vthr);
                  free(current_vthr2);

                  for (int c=0;c<MAX_XL3_CON;c++)
                    if ((0x1<<c) & crateMask)
                      xl3s[c]->ChangeMode(INIT_MODE,slotMasks[c]);
                  return -1;
                }

                // CMOS rate polling is done on bottom and top 8 slot seperately,
                // thus the two slotIter counters and this if/else condition
                if(j < 8){
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter][k];
                }
                else{ 
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF00, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter2][k];
                }

                // now we wait
                for (int t=0;t<2;t++)
                  usleep(SLEEP_TIME);

                if(j < 8){ 
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter][k] - readout_noise[i*16*32+j*32+k];
                }
                else{
                   xl3s[i]->GetCmosTotalCount(slotMasks[i] & 0xFF00, total_count1);
                   readout_noise[i*16*32+j*32+k] = total_count1[slotIter2][k] - readout_noise[i*16*32+j*32+k];
                }

                // Look for rates above 100Hz
                if(readout_noise[i*16*32+j*32+k]/(2*SLEEP_TIME*1e-6) > MAX_NOISE_RATE){
                  //lprintf("Noisy > %3dHz: %2d %2d %2d: %3d %.2f \n",MAX_NOISE_RATE,i,j,k,current_vthr2[i*16*32+j*32+k], readout_noise[i*16*32+j*32+k]/(2*SLEEP_TIME*1e-6));
                  current_vthr2[i*16*32+j*32+k] += 1;
                  count_noisy_channels += 1;
                }
                else{
                  //lprintf("Not Noisy: %2d %2d %2d: Vthr: %3d Noise Freq: %.2f \n",i,j,k,current_vthr2[i*16*32+j*32+k],readout_noise[i*16*32+j*32+k]/(2*SLEEP_TIME*1e-6));
                  break;
                }
                // Try 5 DAC counts maximum
                if(count_noisy_channels >= 5){
                  break;
                }
                // Don't step if channel is already very high
                if(current_vthr2[i*16*32+j*32+k] >= 200){
                  break;
                }
              }
            }
            if(j < 8)
              slotIter++;
            else
              slotIter2++;
          }
        }
      } // end loop over crates in crate mask
    }

    // Print the difference to the log and
    // increment current_vthr2 by one
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            lprintf("Crate/Slot: %2d/%d \n",i,j);
            lprintf(" Ch  Method1  Method2 \n");
            lprintf("--------------------- \n");
            for (int k=0;k<32;k++){
              if(plusOne){
                if(current_vthr2[i*16*32+j*32+k] < 255){
                  current_vthr2[i*16*32+j*32+k]+=1;
                }
              }
              lprintf("%4d %6d %6d \n",k,current_vthr[i*16*32+j*32+k]-vthr_zeros[i*16*32+j*32+k],current_vthr2[i*16*32+j*32+k]-vthr_zeros[i*16*32+j*32+k]);
            }
          }
        }
      }
    }
  } // end if channelTuning

  if (updateDB){
    lprintf("Updating the database\n");
    for (int i=0;i<MAX_XL3_CON;i++){
      if ((0x1<<i) & crateMask){
        for (int j=0;j<16;j++){
          if ((0x1<<j) & slotMasks[i]){
            JsonNode *newdoc = json_mkobject();
            json_append_member(newdoc,"type",json_mkstring("find_noise_2"));
            JsonNode *channels = json_mkarray();
            for (int k=0;k<32;k++){
              JsonNode *one_chan = json_mkobject();
              json_append_member(one_chan,"id",json_mknumber(k));
              json_append_member(one_chan,"zero_used",json_mknumber(vthr_zeros[i*32*16+j*32+k]));
              json_append_member(one_chan,"noiseless",json_mknumber(current_vthr2[i*32*16+j*32+k]));
              JsonNode *points = json_mkarray();
              json_append_element(channels,one_chan);
            }
            json_append_member(newdoc,"channels",channels);
            if (ecal)
              json_append_member(newdoc,"ecal_id",json_mkstring(ecalID));
            json_append_member(newdoc,"pass",json_mkbool(1)); //FIXME

            PostDebugDoc(i,j,newdoc);
            json_delete(newdoc);
          }
        }
      }
    }
  }

  for (int c=0;c<MAX_XL3_CON;c++)
    if ((0x1<<c) & crateMask)
      xl3s[c]->ChangeMode(INIT_MODE,slotMasks[c]);

  mtc->DisablePulser();

  }
  catch(const char* s){
    lprintf("FindNoise: %s\n",s);
  }

  free(readout_noise);
  free(vthr_zeros);
  free(current_vthr);
  free(current_vthr2);
  lprintf("Finished Find noise\n");

  return 0;
}
