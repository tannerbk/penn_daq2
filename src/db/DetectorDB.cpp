#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "Globals.h"
#include "DetectorDB.h"

PGconn* ConnectToDetectorDB(){

  char connect[64];
  sprintf(connect, "host=%s dbname=%s user=%s connect_timeout=15",
          DETECTOR_DB_HOST, DETECTOR_DB_NAME, DETECTOR_DB_USERNAME);

  PGconn *detectorDB = PQconnectdb(connect);

  if(PQstatus(detectorDB) == CONNECTION_BAD){
    char* error = PQerrorMessage(detectorDB);
    lprintf("ProduceRunTableProc: Connection to database failed. Error %s \n", error);
    return detectorDB;
  }

  return detectorDB;

}

void CloseDetectorDBConnection(PGconn* detectorDB){

  PQfinish(detectorDB);
}

int CheckResultStatus(PGresult* qResult, PGconn* detectorDB){

  if(PQresultStatus(qResult) != PGRES_COMMAND_OK &&
     PQresultStatus(qResult) != PGRES_TUPLES_OK){
    char* error = PQresStatus(PQresultStatus(qResult));
    lprintf("Query to DetectorDB failed %s.\n", error);
    PQclear(qResult);
    PQfinish(detectorDB);
    return 1;
  }

  return 0;
}

// Function to turn int* [size] to string in order to push to detector DB
void AppendStringIntArray(int* array, char* buffer, int size){

  char returnstring[512] = "'{";

  for(int i= 0; i < size; i++){
    if(i != (size-1)){
      sprintf(returnstring + strlen(returnstring), "%d, ", array[i]);
    }
    else{
      sprintf(returnstring + strlen(returnstring), "%d", array[i]);
    }
  }

  sprintf(returnstring + strlen(returnstring), "}'");
  strcpy(buffer, returnstring);
}

// Function to turn JsonNode* [size] to string in order to push to detector DB
void AppendStringArray(JsonNode* array, char* buffer, int size){

  int data[size];
  char returnstring[512] = "'{";

  for(int i= 0; i < size; i++){
    data[i] = json_get_number(json_find_element(array,i));
    if(i != (size-1)){
      sprintf(returnstring + strlen(returnstring), "%d, ", data[i]);
    }
    else{
      sprintf(returnstring + strlen(returnstring), "%d", data[i]);
    }
  }

  sprintf(returnstring + strlen(returnstring), "}'");
  strcpy(buffer, returnstring);
}

int LoadZDiscToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB){

  char str_zero[512], str_upper[512], str_lower[512], str_max[512];

  JsonNode *zeros = json_find_member(doc,"zero_dac");
  JsonNode *upper = json_find_member(doc,"upper_dac");
  JsonNode *lower = json_find_member(doc,"lower_dac");
  JsonNode *max   = json_find_member(doc,"max_dac");

  AppendStringArray(zeros, str_zero, 32);
  AppendStringArray(upper, str_upper, 32);
  AppendStringArray(lower, str_lower, 32);
  AppendStringArray(max, str_max, 32);

  char ecalid[64] = "";
  sprintf(ecalid, "'%s'", ecalID);

  char query[2048];
  sprintf(query, "INSERT INTO zdisc "
                 "(crate, slot, zero_disc, upper_disc, lower_disc, max_disc, ecalid) "
                 "VALUES (%d, %d, %s, %s, %s, %s, %s) ",
                  crate, slot, str_zero, str_upper, str_lower, str_max, ecalid);

  PGresult *qResult = PQexec(detectorDB, query);

  if(CheckResultStatus(qResult, detectorDB)){
    return 1;
  }

  PQclear(qResult);
  lprintf("Successful pushed zdisc info to detector state database. \n");
  return 0;
}

int LoadFECDocToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB){

  char str_dbid[512];
  char str_vthr[512];
  char str_isetm[512], str_iseta[512], str_tacshift[512], str_scmos[512];
  char str_rmp[512], str_rmpup[512], str_vsi[512], str_vli[512];
  char str_vbal_1[512], str_vbal_0[512];
  char str_tr100delay[512], str_tr20delay[512], str_tr20width[512];
  int dbid[4];

  // IDs
  int mbid =  (int)strtoul(json_get_string(json_find_member(doc,"board_id")), (char**) NULL, 16);
  JsonNode* id = json_find_member(doc, "id"); 
  dbid[0] = (int)strtoul(json_get_string(json_find_member(id,"db0")), (char**) NULL, 16);
  dbid[1] = (int)strtoul(json_get_string(json_find_member(id,"db1")), (char**) NULL, 16);
  dbid[2] = (int)strtoul(json_get_string(json_find_member(id,"db2")), (char**) NULL, 16);
  dbid[3] = (int)strtoul(json_get_string(json_find_member(id,"db3")), (char**) NULL, 16);
  AppendStringIntArray(dbid, str_dbid, 4);

  // Hardware Settings
  JsonNode* hw = json_find_member(doc, "hw");

  int vint  = (int)json_get_number(json_find_member(hw, "vint"));
  int hvref = (int)json_get_number(json_find_member(hw, "hvref"));

  // TCMOS and CSMOS
  JsonNode *scmos = json_find_member(hw, "scmos");
  AppendStringArray(scmos, str_scmos, 32);

  JsonNode* tcmos = json_find_member(hw, "tcmos");
  int vmax = (int)json_get_number(json_find_member(tcmos, "vmax"));
  int tacref = (int)json_get_number(json_find_member(tcmos, "vtacref"));
  JsonNode* isetm = json_find_member(tcmos, "isetm");
  JsonNode* iseta = json_find_member(tcmos, "iseta");
  JsonNode* tacshift = json_find_member(tcmos, "tac_trim");
  AppendStringArray(isetm, str_isetm, 2);
  AppendStringArray(iseta, str_iseta, 2);
  AppendStringArray(tacshift, str_tacshift, 32);

  // TDISC
  JsonNode* tdisc = json_find_member(hw, "tdisc");
  JsonNode* rmp = json_find_member(tdisc, "rmp");
  JsonNode* rmpup = json_find_member(tdisc, "rmpup");
  JsonNode* vsi = json_find_member(tdisc, "vsi");
  JsonNode* vli = json_find_member(tdisc, "vli");
  AppendStringArray(rmp, str_rmp, 8);
  AppendStringArray(rmpup, str_rmpup, 8);
  AppendStringArray(vsi, str_vsi, 8);
  AppendStringArray(vli, str_vli, 8);

  // VBAL
  JsonNode* vbal = json_find_member(hw, "vbal");
  JsonNode* vbal_0 = json_find_element(vbal, 0);
  JsonNode* vbal_1 = json_find_element(vbal, 1);
  AppendStringArray(vbal_0, str_vbal_0, 32);
  AppendStringArray(vbal_1, str_vbal_1, 32);

  // VTHR
  JsonNode *vthr  = json_find_member(hw, "vthr");
  AppendStringArray(vthr, str_vthr, 32);
  // Note: ZDisc handled separately

  // TRIGGER
  JsonNode *tr100 = json_find_member(hw, "tr100");
  JsonNode *tr100delay = json_find_member(tr100, "delay");
  JsonNode *tr20 = json_find_member(hw, "tr20");
  JsonNode *tr20delay = json_find_member(tr20, "delay");
  JsonNode *tr20width = json_find_member(tr20, "width");
  AppendStringArray(tr100delay, str_tr100delay, 32);
  AppendStringArray(tr20delay, str_tr20delay, 32);
  AppendStringArray(tr20width, str_tr20width, 32);

  char ecalid[64] = "";
  sprintf(ecalid, "'%s'", ecalID);

  const int buffer = 2048;
  char query[buffer];
  int size = snprintf(query, buffer, "INSERT INTO fecdoc "
                  "(ecalid, crate, slot, mbid, dbid, vthr, vint, hvref, scmos, "
                  "tcmos_vmax, tcmos_tacref, tcmos_isetm, tcmos_iseta, tcmos_tacshift, "
                  "tdisc_rmp, tdisc_rmpup, tdisc_vsi, tdisc_vli, vbal_0, vbal_1, "
                  "tr100_delay, tr20_delay, tr20_width) "
                  "VALUES (%s, %d, %d, %d, %s, %s, %d, %d,"
                          "%s, %d, %d, %s, %s, %s,"
                          "%s, %s, %s, %s, %s, %s,"
                          "%s, %s, %s)",
                   ecalid, crate, slot, mbid, str_dbid, str_vthr, vint, hvref, 
                   str_scmos, vmax, tacref, str_isetm, str_iseta, str_tacshift,
                   str_rmp, str_rmpup, str_vsi, str_vli, str_vbal_0, str_vbal_1,
                   str_tr100delay, str_tr20delay, str_tr20width);

  if(size >= buffer){
    lprintf("FECDOC SQL query buffer overflow for crate %d, slot %d.\n", crate, slot);
    return 1;
  }

  PGresult *qResult = PQexec(detectorDB, query);

  if(CheckResultStatus(qResult, detectorDB)){
    return 1;
  }

  PQclear(qResult);
  lprintf("Successful pushed fecdoc info to detector state database. \n");

  return 0;
}

int UpdateTriggerStatus(int type, int crate, int slot, int channel, PGconn* detectorDB){

  char updateN100[8] = "True";
  char updateN20[8] = "True";

  if(type == 1){
    strcpy(updateN20, "False");
  }
  else if(type == 2){
    strcpy(updateN100, "False");
  }
  else if(type !=0){
    lprintf("Error getting missing trigger type.\n");
    return 1;
  }

  char query[2048];
  sprintf(query, "INSERT INTO channel_status "
   "(crate,slot,channel,pmt_removed,pmt_reinstalled,low_occupancy,zero_occupancy, "
   "screamer,bad_discriminator,no_n100,no_n20,no_esum,cable_pulled,bad_cable, "
   "resistor_pulled,disable_n100,disable_n20,bad_base_current,high_dropout,bad_data) "
   "SELECT "
   "crate,slot,channel,pmt_removed,pmt_reinstalled,low_occupancy,zero_occupancy, "
   "screamer,bad_discriminator,no_n100,no_n20,no_esum,cable_pulled,bad_cable, "
   "resistor_pulled,disable_n100,disable_n20,bad_base_current,high_dropout,bad_data "
   "FROM channel_status "
   "WHERE crate=%d AND slot=%d AND channel=%d "
   "ORDER by timestamp DESC limit 1",
   crate, slot, channel);

  PGresult *qResult = PQexec(detectorDB, query);
  if(CheckResultStatus(qResult, detectorDB)){
    return 1;
  }
  PQclear(qResult);

  sprintf(query, "UPDATE channel_status "
    "SET no_n100=%s, no_n20=%s WHERE crate = %d and slot = %d AND "
    "channel = %d AND timestamp = (SELECT max(timestamp) FROM channel_status "
    "WHERE crate=%d and slot = %d and channel = %d)", updateN100, updateN20,
    crate, slot, channel, crate, slot, channel);

  qResult = PQexec(detectorDB, query);
  if(CheckResultStatus(qResult, detectorDB)){
    return 1;
  }
  PQclear(qResult);

  lprintf("Succesfully updated detector DB with missing triggers: N100 = %s and N20 = %s.\n",updateN100, updateN20);
  return 0;
}

int DetectorSlotMask(uint32_t crateMask, uint32_t *slotMasks, PGconn* detectorDB){

  uint32_t newSlotMasks[MAX_XL3_CON] = {0};
  PGresult *qResult;

  char query[2048];
  sprintf(query, "SELECT last_value FROM run_number");
  qResult = PQexec(detectorDB, query);
  if(CheckResultStatus(qResult, detectorDB)){
    return 1;
  }
  char* run_str;
  run_str = PQgetvalue(qResult, 0, 0);
  int run = strtoul(run_str, NULL, 0);
  PQclear(qResult);

  lprintf("Using run %d to find missing slots.\n", run);

  sprintf(query, "SELECT crate, slot FROM detector_state "
                  "WHERE run = %d ORDER BY crate, slot", run);
  qResult = PQexec(detectorDB, query);
  if(CheckResultStatus(qResult, detectorDB)){
    return 1;
  }
  for(int i = 0; i < PQntuples(qResult); i++){
    char* crate_str = PQgetvalue(qResult, i, 0); 
    char* slot_str = PQgetvalue(qResult, i, 1); 
    int crateNum = strtoul(crate_str, NULL, 0);
    int slotNum = strtoul(slot_str, NULL, 0);
    newSlotMasks[crateNum] |= (1 << slotNum);
  }

  for(int i = 0; i < MAX_XL3_CON; i++){
    if((0x1<<i) & crateMask){
      slotMasks[i] &= newSlotMasks[i];
    }
  }
  PQclear(qResult);

  return 0;
}

