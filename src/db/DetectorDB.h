#ifndef _DetectorDB_H
#define _DetectorDB_H

#include <libpq-fe.h>

#include "Json.h"

PGconn* ConnectToDetectorDB();
void CloseDetectorDBConnection(PGconn* detectorDB);
int CheckResultsStatus(PGresult* qResult, PGconn* detectorDB);

// These are called ultimately from Create FEC Doc command
// Load zdisc values to the detector database. These are measured values and are not
// a part of the Fec Doc, which only stores hardware settings
int LoadZDiscToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB);

// Load measured GT valid lengths to detector database
int LoadGTValidsToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB);

// Load channel problems to the detector database
int LoadChannelStatusToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGConn* detectorDB);

// Load FEC Documents to the detector database
int LoadFECDocToDetectorDB(JsonNode* doc, int crate, int slot, const char* ecalID, PGconn* detectorDB);

// Update channel status in detector database for bad zdisc
int LoadBadDiscToDetectorDB(int crate, int slot, int channel, PGconn* detectorDB);

// Insert current status into channel status before updating (FIXME)
int UpdateChannelStatus(int crate, int slot, int channel, PGconn* detectorDB);

// Tool used for formatting SQL query for arrays size
void AppendStringArray(JsonNode* array, char* buffer, int size);
void AppendStringIntArray(int* array, char* buffer, int size);
void AppendStringFloatArray(float* array, char* buffer, int size);

// This is used in SeeRefl test to update whether the triggers are working
int UpdateTriggerStatus(int type, int crate, int slot, int channel, PGconn* detectorDB);

// Used to get slot mask according to detectordb
int DetectorSlotMask(uint32_t crateMask, uint32_t *slotMask, PGconn* detectorDB);


#endif

