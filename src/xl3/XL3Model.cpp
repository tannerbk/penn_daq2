#include "Globals.h"
#include "DBTypes.h"
#include "XL3Registers.h"
#include "DacNumber.h"

#include "XL3Link.h"
#include "XL3Model.h"

XL3Model::XL3Model(int crateNum)
{
  fCrateNum = crateNum; 
  fCommandNum = 0;
  for (int i=0;i<16;i++){
    fFECs[i].mbID = 0x0;
    for (int j=0;j<4;j++)
      fFECs[i].dbID[j] = 0x0;
  }
  fLink = new XL3Link(crateNum);
}

XL3Model::~XL3Model()
{
  delete fLink;
}

int XL3Model::RW(uint32_t address, uint32_t data, uint32_t *result)
{
  XL3Packet packet;
  packet.header.packetType = FAST_CMD_ID;
  FastCmdArgs *args = (FastCmdArgs *) packet.payload;
  args->command.address = address;
  args->command.data = data;
  SwapLongBlock(&args->command.address,1);
  SwapLongBlock(&args->command.data,1);

  SendCommand(&packet);

  SwapLongBlock(&args->command.data,1);
  *result = args->command.data;
  SwapShortBlock(&args->command.flags,1);
  return (int) args->command.flags;
}

int XL3Model::CheckLock()
{
  if (!fLink->IsConnected())
    return NO_CONNECTION_FLAG;
  if (fLink->IsLocked())
    return BUSY_CONNECTION_FLAG;

  return 0;
}

int XL3Model::SendCommand(XL3Packet *packet,int withResponse, int timeout)
{
  uint16_t type = packet->header.packetType;
  packet->header.packetNum = fCommandNum;
  int recvCommandNum = fCommandNum;
  recvCommandNum %= 65536;
  fCommandNum++;
  fCommandNum %= 65536;
  fLink->SendPacket(packet);
  if (withResponse){
    int numPackets = 0;
    int maxTries = 100;
    int err;
    // look for the response. If you get the wrong packet type, try again, but
    // eventually raise an exception
    do{
      err = fLink->GetNextPacket(packet,timeout);
      numPackets++;
      if (err)
        throw "SendCommand: GetNextPacket timed out";
      if (numPackets > maxTries)
        throw "SendCommand: Got too many wrong packet types";
      if (packet->header.packetNum > recvCommandNum && ((65536-packet->header.packetNum) + recvCommandNum < 5000))
        throw "SendCommand: Packet Num too high";
      if (packet->header.packetType != type)
        lprintf("Got %02x instead of %02x\n",packet->header.packetType,type);
    }while(packet->header.packetType != type || packet->header.packetNum != recvCommandNum);
  }
  return 0;
}

int XL3Model::DeselectFECs()
{
  XL3Packet packet;
  packet.header.packetType = DESELECT_FECS_ID;
  SendCommand(&packet);
  return 0;
}

int XL3Model::GetMultiFCResults(int numCmds, int packetNum, uint32_t *result, int timeout)
{
  packetNum %= 65536;
  Command command;
  int cmdNum = 0;
  int started = 0;
  int busErrors = 0;
  while (numCmds > cmdNum){
    int err = fLink->GetNextCmdAck(&command,timeout);
    if (err)
      throw "GetMultiFCResults: GetNextCmdAck timed out";
    if (command.packetNum != packetNum){
      if (started)
        throw "GetMultiFCResults: Got wrong packet number";
      else
        continue;
    }
    if (command.cmdNum != cmdNum){
      lprintf("Expected %d, got %d\n",cmdNum,command.cmdNum);
      throw "GetMultiFCResults: Got wrong command number";
    }
    busErrors += command.flags;
    *(result + cmdNum) = command.data;
    cmdNum++;
  }
  return 0;
}

int XL3Model::GetCaldTestResults(uint16_t *point_buf, uint16_t *adc_buf)
{

  int point_count = 0;
  int current_slot = 0;
  int current_point = 0;
  XL3Packet packet;

  while (1){
    int err = fLink->GetNextPacket(&packet,20);
    if (err)
      throw "GetCaldTestResults: GetNextPacket timed out";
    if (packet.header.packetNum > (fCommandNum-1))
      throw "GetCaldTestResults: Packet num too high";
    if (packet.header.packetType == CALD_RESPONSE_ID){
      CaldResponsePacket *response = (CaldResponsePacket *) packet.payload;
      SwapShortBlock(response,sizeof(CaldResponsePacket)/sizeof(uint16_t));
      if (response->slot != current_slot){
        current_slot = response->slot;
        current_point = 0;
      }
      for (int j=0;j<100;j++){
        if (response->point[j] != 0){
          point_buf[current_slot*10000+current_point] = response->point[j];
          point_buf[0] = response->point[j];
          adc_buf[current_slot*4*10000+current_point*4+0] = response->adc0[j];
          adc_buf[current_slot*4*10000+current_point*4+1] = response->adc1[j];
          adc_buf[current_slot*4*10000+current_point*4+2] = response->adc2[j];
          adc_buf[current_slot*4*10000+current_point*4+3] = response->adc3[j];
          current_point++;
          point_count++;
        }
      }
    }else if (packet.header.packetType == CALD_TEST_ID){
      // we must be finished
      return point_count;
    }
  }
  // never get here
  return 0;
}



int XL3Model::UpdateCrateConfig(uint16_t slotMask)
{
  XL3Packet packet;
  packet.header.packetType = BUILD_CRATE_CONFIG_ID;
  BuildCrateConfigArgs *args = (BuildCrateConfigArgs *) packet.payload;
  BuildCrateConfigResults *results = (BuildCrateConfigResults *) packet.payload;
  args->slotMask = slotMask;
  SwapLongBlock(packet.payload,sizeof(BuildCrateConfigArgs)/sizeof(uint32_t));
  try{
    SendCommand(&packet);
    int errors = results->errorFlags;
    for (int i=0;i<16;i++){
      if ((0x1<<i) & slotMask){
        fFECs[i] = results->hwareVals[i];
        SwapShortBlock(&(fFECs[i].mbID),1);
        SwapShortBlock((fFECs[i].dbID),4);
      }
    }
    DeselectFECs();
  }
  catch(const char* s){
    lprintf("Error: Unable to update crate configuration\n");
    throw s;
  }
  return 0;
}

int XL3Model::ChangeMode(int mode, uint16_t dataAvailMask)
{
  XL3Packet packet;
  packet.header.packetType = CHANGE_MODE_ID;
  ChangeModeArgs *args = (ChangeModeArgs *) packet.payload;
  args->mode = mode;
  args->dataAvailMask = dataAvailMask;
  SwapLongBlock(packet.payload,sizeof(ChangeModeArgs)/sizeof(uint32_t));
  SendCommand(&packet);
  return 0;
}

int XL3Model::ConfigureCrate(FECConfiguration *fecs)
{
  for (int i=0;i<16;i++){
    fFECs[i] = fecs[i];
  }
  return 0;
}

int XL3Model::GetCmosTotalCount(uint16_t slotMask, uint32_t totalCount[][32])
{
  if (slotMask != 0x0){
    XL3Packet packet;
    CheckTotalCountArgs *args = (CheckTotalCountArgs *) packet.payload;
    CheckTotalCountResults *results = (CheckTotalCountResults *) packet.payload;
    packet.header.packetType = CHECK_TOTAL_COUNT_ID;
    args->slotMask = slotMask;
    for (int i=0;i<16;i++)
      args->channelMasks[i] = 0xFFFFFFFF;
    SwapLongBlock(args,sizeof(CheckTotalCountArgs)/sizeof(uint32_t));
    SendCommand(&packet);
    SwapLongBlock(results,sizeof(CheckTotalCountResults)/sizeof(uint32_t));
    for (int i=0;i<8;i++)
      for (int j=0;j<32;j++){
        totalCount[i][j] = results->count[i*32+j];
      }
  }else{
    for (int i=0;i<8;i++)
      for (int j=0;j<32;j++)
        totalCount[i][j] = 0;

  }
  return 0;
}

int XL3Model::LoadsDac(uint32_t dacNum, uint32_t dacValue, int slotNum)
{
  XL3Packet packet;
  packet.header.packetType = LOADSDAC_ID;
  LoadsDacArgs *args = (LoadsDacArgs *) packet.payload;
  LoadsDacResults *results = (LoadsDacResults *) packet.payload;
  args->slotNum = slotNum;
  args->dacNum = dacNum;
  args->dacValue = dacValue;
  SwapLongBlock(args,sizeof(LoadsDacArgs)/sizeof(uint32_t));
  SendCommand(&packet);
  SwapLongBlock(results,sizeof(LoadsDacResults)/sizeof(uint32_t));
  return results->errorFlags;
}

int XL3Model::MultiLoadsDac(int numDacs, uint32_t *dacNums, uint32_t *dacValues, uint32_t *slotNums)
{
  XL3Packet packet;
  packet.header.packetType = MULTI_LOADSDAC_ID;
  MultiLoadsDacArgs *args = (MultiLoadsDacArgs *) packet.payload;
  MultiLoadsDacResults *results = (MultiLoadsDacResults *) packet.payload;
  args->numDacs = numDacs;
  for (int j=0;j<numDacs;j++){
    if (j>=50) break;
    args->dacs[j].slotNum = slotNums[j];
    args->dacs[j].dacNum = dacNums[j];
    args->dacs[j].dacValue = (dacValues[j] < 256 && dacValues[j] >= 0) ? dacValues[j] : 255;
  }
  SwapLongBlock(args,numDacs*3+1);
  SendCommand(&packet);
  SwapLongBlock(results,sizeof(MultiLoadsDacResults)/sizeof(uint32_t));
  return results->errorFlags;
}

int32_t XL3Model::ReadOutBundles(int slotNum, uint32_t *pmtBuffer, int limit, int checkLimit)
{
  XL3Packet packet;
  MultiCommand *commands = (MultiCommand *) packet.payload;
  int count;
  int error = 0;
  uint32_t result,diff;
  uint32_t selectReg = FEC_SEL*slotNum;

  try{
    // find out how many bundles are in the fifo
    RW(FIFO_DIFF_PTR_R + selectReg + READ_REG,0x0,&diff);
    diff &= 0xFFFFF;
    diff = diff - (diff%3);

    // check if there are more bundles than expected
    if (checkLimit){
      if ((3*limit) < diff){
        if (diff > 1.5*(3*limit))
          lprintf("Memory level much higher than expected (%d > %d). "
              "Possible fifo overflow\n",diff,3*limit);
        else
          lprintf("Memory level over expected (%d > %d)\n",diff,3*limit);
        diff = 3*limit;
      }else if ((3*limit) > diff){
        lprintf("Memory level under expected (%d < %d)\n",diff,3*limit);
      }
    }else{
      //diff = diff >= 3*limit ? 3*limit : diff;
    }

    // lets read out the bundles!
    lprintf("Attempting to read %d bundles\n",diff/3);
    // we need to read it out MAX_FEC_COMMANDS at a time
    int readsLeft = diff;
    int thisRead;
    while (readsLeft != 0){
      if (readsLeft > MAX_FEC_COMMANDS-1000)
        thisRead = MAX_FEC_COMMANDS-1000;
      else
        thisRead = readsLeft;
      // queue up all the reads
      packet.header.packetType = READ_PEDESTALS_ID;
      ReadPedestalsArgs *args = (ReadPedestalsArgs *) packet.payload;
      ReadPedestalsResults *results = (ReadPedestalsResults *) packet.payload;
      args->slot = slotNum;
      args->reads = thisRead;
      SwapLongBlock(args,sizeof(ReadPedestalsArgs)/sizeof(uint32_t));
      SendCommand(&packet);
      SwapLongBlock(results,sizeof(ReadPedestalsResults)/sizeof(uint32_t));
      thisRead = results->readsQueued;

      if (thisRead > 0){
        // now wait for the data to come
        GetMultiFCResults(thisRead, fCommandNum-1,pmtBuffer+(diff-readsLeft));
        readsLeft -= thisRead;
      }
    }

    count = diff / 3;
    DeselectFECs();
  }
  catch(const char* s){
    lprintf("There was a network error trying to read out bundles!\n");
    throw s;
  }
  return count;
}

int XL3Model::LoadCrateAddr(uint16_t slotMask)
{
  uint32_t result;
  for (int i=0;i<16;i++){
    if ((0x1<<i) & slotMask){
      RW(GENERAL_CSR_R + FEC_SEL*i + WRITE_REG, 0x0 | (fCrateNum << FEC_CSR_CRATE_OFFSET),&result);
    }
  }
  DeselectFECs();
  return 0;
}

int XL3Model::SetCratePedestals(uint16_t slotMask, uint32_t pattern)
{
  XL3Packet packet;
  packet.header.packetType = SET_CRATE_PEDESTALS_ID;
  SetCratePedestalsArgs *args = (SetCratePedestalsArgs *) packet.payload;
  args->slotMask = slotMask;
  args->pattern = pattern;
  SwapLongBlock(args,sizeof(SetCratePedestalsArgs)/sizeof(uint32_t));
  SendCommand(&packet);
  return 0;
}

int XL3Model::SetupChargeInjection(uint32_t slotMask, uint32_t chanMask, uint32_t dacValue)
{
  XL3Packet packet;
  packet.header.packetType = MULTI_SETUP_CHARGE_INJ_ID;
  MultiSetUpChargeInjArgs *args = (MultiSetUpChargeInjArgs *) packet.payload;
  args->slotMask = slotMask;
  // Same channel mask and DAC value get used for all slots in the slot mask
  for(int i = 0; i < 16; i++){
    if((0x1<<i) & slotMask){
      args->channelMasks[i] = chanMask;
      args->dacValues[i] = dacValue;
    }
  }
  SwapLongBlock(args,sizeof(MultiSetUpChargeInjArgs)/sizeof(int32_t));
  SendCommand(&packet);
  return 0;
}

int XL3Model::LoadTacbits(uint32_t slotNum, uint16_t *tacbits)
{
  XL3Packet packet;
  packet.header.packetType = LOAD_TACBITS_ID;
  LoadTacBitsArgs *args = (LoadTacBitsArgs *) packet.payload;
  args->crateNum = fCrateNum;
  args->selectReg = FEC_SEL*slotNum;
  for (int j=0;j<32;j++){
    args->tacBits[j] = tacbits[j];
  }
  SwapLongBlock(packet.payload,2);
  SwapShortBlock(packet.payload+8,32);
  SendCommand(&packet);
  lprintf("TAC bits loaded\n");

  return 0;
}

int XL3Model::SetAlarmDacs(uint32_t *dacs)
{
  XL3Packet packet;
  packet.header.packetType = SET_ALARM_DAC_ID;
  SetAlarmDacArgs *args = (SetAlarmDacArgs *) packet.payload;
  for (int i=0;i<3;i++)
    args->dacs[i] = dacs[i];
  SwapLongBlock(packet.payload,sizeof(SetAlarmDacArgs)/sizeof(uint32_t));
  SendCommand(&packet);
  return 0;
}


