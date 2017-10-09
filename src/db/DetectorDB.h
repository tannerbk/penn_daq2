#ifndef _DetectorDB_H
#define _DetectorDB_H

#include <libpq-fe.h>

#include "JSon.h"

PGconn* ConnectToDetectorDB();
void CloseDetectorDBConnection(PGconn* detectorDB);
int CheckResultsStatus(PGresult* qResult, PGconn* detectorDB);

// These are called ultimately from Create FEC Doc command
int LoadZDiscToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB);

int LoadFECDocToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB);

// Tool used for formatting SQL query for arrays size
void AppendStringArray(JsonNode* array, char* buffer, int size);
void AppendStringIntArray(int* array, char* buffer, int size);

// This is used in SeeRefl Test
int UpdateTriggerStatus(int type, int crate, int slot, int channel, PGconn* detectorDB);

// Used to get slot mask according to detectordb
int DetectorSlotMask(uint32_t crateMask, uint32_t *slotMask, PGconn* detectorDB);


#endif

