#ifndef _DB_H
#define _DB_H

#include <stdint.h>
#include <libpq-fe.h>

#include "Json.h"
#include "DBTypes.h"

#define DEF_DB_ADDRESS "localhost"
#define DEF_DB_PORT "5984"
#define DEF_DB_USERNAME ""
#define DEF_DB_PASSWORD ""
#define DEF_DB_BASE_NAME "penndb1"
#define DEF_DB_VIEWDOC "_design/view_doc/_view"

// Critical ECAL tests, must run with each ECAL
const static int ntests = 5;
static const char test_map[ntests][20] = {
    "crate_cbal","zdisc","set_ttot","cmos_m_gtvalid","find_noise_2"};

// A couple non-critical test that are useful to keep track of errors for
const static int nntests = 6;
static const char test_map_ncrit[nntests][20] = 
    {"ped_run", "cgt_test", "get_ttot", "fec_test", 
     "disc_check", "ped_run_by_channel"};

// Map from test to error bit
enum Bit { cbal_fail = 0, zdisc_fail = 1, sttot_fail = 2, 
           gtvalid_fail = 3, ped_fail = 4, cgt_fail = 5, 
           gttot_fail = 6 , fec_test_fail = 7, disc_check_fail = 8,
           ped_run_by_channel = 9};

int GetNewID(char* newid);

int ParseFECHw(JsonNode* value,MB* mb);
int ParseFECDebug(JsonNode* value,MB* mb);
int SwapFECDB(MB* mb);
int ParseMTC(JsonNode* value,MTC* mtc);

int CreateFECDBDoc(int crate, int card, JsonNode** doc_p, JsonNode *ecal_doc);
int AddECALTestResults(JsonNode *fec_doc, JsonNode *test_doc, unsigned int* chan_prob_array);
int PostFECDBDoc(int crate, int slot, JsonNode *doc);
int UpdateFECDBDoc(JsonNode *doc);
int GenerateFECDocFromECAL(uint32_t crateMask, uint32_t *slotMasks, const char* id, PGconn* detectorDB);

void SetupDebugDoc(int crateNum, int slotNum, JsonNode* doc);
int PostDebugDoc(int crate, int card, JsonNode* doc, int updateConfig=1);
int PostDebugDocWithID(int crate, int card, char *id, JsonNode* doc);
int PostECALDoc(uint32_t crateMask, uint32_t *slotMasks, char *logfile, char *id);

int UpdateLocation(uint16_t *ids, int *crates, int *slots, int *positions, int boardcount);
int RemoveFromConfig(JsonNode *config_doc, char ids[][5], int boardcount);

#endif

