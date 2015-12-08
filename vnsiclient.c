/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
 *      Copyright (C) 2015 Team KODI
 *
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with KODI; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "vnsiclient.h"
#include "vnsi.h"
#include "config.h"
#include "vnsicommand.h"
#include "recordingscache.h"
#include "streamer.h"
#include "vnsiserver.h"
#include "recplayer.h"
#include "vnsiosd.h"
#include "requestpacket.h"
#include "responsepacket.h"
#include "hash.h"
#include "channelfilter.h"
#include "channelscancontrol.h"

#include <stdlib.h>
#include <stdio.h>
#include <map>

#include <vdr/recording.h>
#include <vdr/channels.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>
#include <vdr/timers.h>
#include <vdr/menu.h>
#include <vdr/device.h>

cMutex cVNSIClient::m_timerLock;
bool cVNSIClient::m_inhibidDataUpdates = false;

cVNSIClient::cVNSIClient(int fd, unsigned int id, const char *ClientAdr)
  : m_Id(id),
    m_loggedIn(false),
    m_StatusInterfaceEnabled(false),
    m_Streamer(NULL),
    m_isStreaming(false),
    m_bSupportRDS(false),
    m_ClientAddress(ClientAdr),
    m_RecPlayer(NULL),
    m_Osd(NULL),
    m_ChannelScanControl(this)
{
  m_socket.SetHandle(fd);

  Start();
}

cVNSIClient::~cVNSIClient()
{
  DEBUGLOG("%s", __FUNCTION__);
  StopChannelStreaming();
  m_ChannelScanControl.StopScan();
  m_socket.close(); // force closing connection
  Cancel(10);
  DEBUGLOG("done");
}

void cVNSIClient::Action(void)
{
  uint32_t channelID;
  uint32_t requestID;
  uint32_t opcode;
  uint32_t dataLength;
  uint8_t* data;

  while (Running())
  {
    if (!m_socket.read((uint8_t*)&channelID, sizeof(uint32_t))) break;
    channelID = ntohl(channelID);

    if (channelID == 1)
    {
      if (!m_socket.read((uint8_t*)&requestID, sizeof(uint32_t), 10000)) break;
      requestID = ntohl(requestID);

      if (!m_socket.read((uint8_t*)&opcode, sizeof(uint32_t), 10000)) break;
      opcode = ntohl(opcode);

      if (!m_socket.read((uint8_t*)&dataLength, sizeof(uint32_t), 10000)) break;
      dataLength = ntohl(dataLength);
      if (dataLength > 200000) // a random sanity limit
      {
        ERRORLOG("dataLength > 200000!");
        break;
      }

      if (dataLength)
      {
        try {
          data = new uint8_t[dataLength];
        } catch (const std::bad_alloc &) {
          ERRORLOG("Extra data buffer malloc error");
          break;
        }

        if (!m_socket.read(data, dataLength, 10000))
        {
          ERRORLOG("Could not read data");
          free(data);
          break;
        }
      }
      else
      {
        data = NULL;
      }

      DEBUGLOG("Received chan=%u, ser=%u, op=%u, edl=%u", channelID, requestID, opcode, dataLength);

      if (!m_loggedIn && (opcode != VNSI_LOGIN))
      {
        ERRORLOG("Clients must be logged in before sending commands! Aborting.");
        if (data) free(data);
        break;
      }

      cRequestPacket req(requestID, opcode, data, dataLength);

      processRequest(req);
    }
    else
    {
      ERRORLOG("Incoming channel number unknown");
      break;
    }
  }

  /* If thread is ended due to closed connection delete a
     possible running stream here */
  StopChannelStreaming();
  m_ChannelScanControl.StopScan();

  // Shutdown OSD
  if (m_Osd)
  {
    delete m_Osd;
    m_Osd = NULL;
  }
}

bool cVNSIClient::StartChannelStreaming(cResponsePacket &resp, const cChannel *channel, int32_t priority, uint8_t timeshift, uint32_t timeout)
{
  m_Streamer    = new cLiveStreamer(m_Id, m_bSupportRDS, timeshift, timeout);
  m_isStreaming = m_Streamer->StreamChannel(channel, priority, &m_socket, &resp);
  return m_isStreaming;
}

void cVNSIClient::StopChannelStreaming()
{
  m_isStreaming = false;
  if (m_Streamer)
  {
    delete m_Streamer;
    m_Streamer = NULL;
  }
}

void cVNSIClient::TimerChange(const cTimer *Timer, eTimerChange Change)
{
  TimerChange();
}

void cVNSIClient::TimerChange()
{
  cMutexLock lock(&m_msgLock);

  if (m_StatusInterfaceEnabled)
  {
    cResponsePacket resp;
    resp.initStatus(VNSI_STATUS_TIMERCHANGE);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
  }
}

void cVNSIClient::ChannelsChange()
{
  cMutexLock lock(&m_msgLock);

  if (!m_StatusInterfaceEnabled)
    return;

  cResponsePacket resp;
  resp.initStatus(VNSI_STATUS_CHANNELCHANGE);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::RecordingsChange()
{
  cMutexLock lock(&m_msgLock);

  if (!m_StatusInterfaceEnabled)
    return;

  cResponsePacket resp;
  resp.initStatus(VNSI_STATUS_RECORDINGSCHANGE);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::EpgChange()
{
  cMutexLock lock(&m_msgLock);

  if (!m_StatusInterfaceEnabled)
    return;

#if VDRVERSNUM >= 20301
  cStateKey SchedulesStateKey(true);
  const cSchedules *schedules = cSchedules::GetSchedulesRead(SchedulesStateKey);
  if (!schedules)
  {
    return;
  }
#else
  cSchedulesLock MutexLock;
  const cSchedules *schedules = cSchedules::Schedules(MutexLock);
  if (!schedules)
    return;
#endif

  std::map<int, sEpgUpdate>::iterator it;
  for (const cSchedule *schedule = schedules->First(); schedule; schedule = schedules->Next(schedule))
  {
    const cEvent *lastEvent =  schedule->Events()->Last();
    if (!lastEvent)
      continue;

#if VDRVERSNUM >= 20301
    LOCK_CHANNELS_READ;
    const cChannel *channel = Channels->GetByChannelID(schedule->ChannelID());
#else
    Channels.Lock(false);
    const cChannel *channel = Channels.GetByChannelID(schedule->ChannelID());
    Channels.Unlock();
#endif

    if (!channel)
      continue;

    if (!VNSIChannelFilter.PassFilter(*channel))
      continue;

    uint32_t channelId = CreateStringHash(schedule->ChannelID().ToString());
    it = m_epgUpdate.find(channelId);
    if (it != m_epgUpdate.end() && it->second.lastEvent >= lastEvent->StartTime())
    {
      continue;
    }

    if (it->second.attempts > 3)
    {
      continue;
    }
    it->second.attempts++;

    INFOLOG("Trigger EPG update for channel %s, id: %d", channel->Name(), channelId);

    cResponsePacket resp;
    resp.initStatus(VNSI_STATUS_EPGCHANGE);
    resp.add_U32(channelId);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
  }
#if VDRVERSNUM >= 20301
  SchedulesStateKey.Remove();
#endif
}

void cVNSIClient::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
  cMutexLock lock(&m_msgLock);

  if (m_StatusInterfaceEnabled)
  {
    cResponsePacket resp;
    resp.initStatus(VNSI_STATUS_RECORDING);
    resp.add_U32(Device->CardIndex());
    resp.add_U32(On);
    if (Name)
      resp.add_String(Name);
    else
      resp.add_String("");

    if (FileName)
      resp.add_String(FileName);
    else
      resp.add_String("");

    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
  }
}

void cVNSIClient::OsdStatusMessage(const char *Message)
{
  cMutexLock lock(&m_msgLock);

  if (m_StatusInterfaceEnabled && Message)
  {
    /* Ignore this messages */
    if (strcasecmp(Message, trVDR("Channel not available!")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete timer?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete recording?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Press any key to cancel shutdown")) == 0) return;
    else if (strcasecmp(Message, trVDR("Press any key to cancel restart")) == 0) return;
    else if (strcasecmp(Message, trVDR("Editing - shut down anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Recording - shut down anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("shut down anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Recording - restart anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Editing - restart anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete channel?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Timer still recording - really delete?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete marks information?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete resume information?")) == 0) return;
    else if (strcasecmp(Message, trVDR("CAM is in use - really reset?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Really restart?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Stop recording?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Cancel editing?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Cutter already running - Add to cutting queue?")) == 0) return;
    else if (strcasecmp(Message, trVDR("No index-file found. Creating may take minutes. Create one?")) == 0) return;

    cResponsePacket resp;
    resp.initStatus(VNSI_STATUS_MESSAGE);
    resp.add_U32(0);
    resp.add_String(Message);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
  }
}

void cVNSIClient::ChannelChange(const cChannel *Channel)
{
  cMutexLock lock(&m_msgLock);
  if (m_isStreaming && m_Streamer)
  {
    m_Streamer->RetuneChannel(Channel);
  }
}

bool cVNSIClient::processRequest(cRequestPacket &req)
{
  cMutexLock lock(&m_msgLock);

  bool result = false;
  switch(req.getOpCode())
  {
    /** OPCODE 1 - 19: VNSI network functions for general purpose */
    case VNSI_LOGIN:
      result = process_Login(req);
      break;

    case VNSI_GETTIME:
      result = process_GetTime(req);
      break;

    case VNSI_ENABLESTATUSINTERFACE:
      result = process_EnableStatusInterface(req);
      break;

    case VNSI_PING:
      result = process_Ping(req);
      break;

    case VNSI_GETSETUP:
      result = process_GetSetup(req);
      break;

    case VNSI_STORESETUP:
      result = process_StoreSetup(req);
      break;

    /** OPCODE 20 - 39: VNSI network functions for live streaming */
    case VNSI_CHANNELSTREAM_OPEN:
      result = processChannelStream_Open(req);
      break;

    case VNSI_CHANNELSTREAM_CLOSE:
      result = processChannelStream_Close();
      break;

    case VNSI_CHANNELSTREAM_SEEK:
      result = processChannelStream_Seek(req);
      break;

    /** OPCODE 40 - 59: VNSI network functions for recording streaming */
    case VNSI_RECSTREAM_OPEN:
      result = processRecStream_Open(req);
      break;

    case VNSI_RECSTREAM_CLOSE:
      result = processRecStream_Close(req);
      break;

    case VNSI_RECSTREAM_GETBLOCK:
      result = processRecStream_GetBlock(req);
      break;

    case VNSI_RECSTREAM_POSTOFRAME:
      result = processRecStream_PositionFromFrameNumber(req);
      break;

    case VNSI_RECSTREAM_FRAMETOPOS:
      result = processRecStream_FrameNumberFromPosition(req);
      break;

    case VNSI_RECSTREAM_GETIFRAME:
      result = processRecStream_GetIFrame(req);
      break;

    case VNSI_RECSTREAM_GETLENGTH:
      result = processRecStream_GetLength(req);
      break;


    /** OPCODE 60 - 79: VNSI network functions for channel access */
    case VNSI_CHANNELS_GETCOUNT:
      result = processCHANNELS_ChannelsCount(req);
      break;

    case VNSI_CHANNELS_GETCHANNELS:
      result = processCHANNELS_GetChannels(req);
      break;

    case VNSI_CHANNELGROUP_GETCOUNT:
      result = processCHANNELS_GroupsCount(req);
      break;

    case VNSI_CHANNELGROUP_LIST:
      result = processCHANNELS_GroupList(req);
      break;

    case VNSI_CHANNELGROUP_MEMBERS:
      result = processCHANNELS_GetGroupMembers(req);
      break;

    case VNSI_CHANNELS_GETCAIDS:
      result = processCHANNELS_GetCaids(req);
      break;

    case VNSI_CHANNELS_GETWHITELIST:
      result = processCHANNELS_GetWhitelist(req);
      break;

    case VNSI_CHANNELS_GETBLACKLIST:
      result = processCHANNELS_GetBlacklist(req);
      break;

    case VNSI_CHANNELS_SETWHITELIST:
      result = processCHANNELS_SetWhitelist(req);
      break;

    case VNSI_CHANNELS_SETBLACKLIST:
      result = processCHANNELS_SetBlacklist(req);
      break;

    /** OPCODE 80 - 99: VNSI network functions for timer access */
    case VNSI_TIMER_GETCOUNT:
      result = processTIMER_GetCount(req);
      break;

    case VNSI_TIMER_GET:
      result = processTIMER_Get(req);
      break;

    case VNSI_TIMER_GETLIST:
      result = processTIMER_GetList(req);
      break;

    case VNSI_TIMER_ADD:
      result = processTIMER_Add(req);
      break;

    case VNSI_TIMER_DELETE:
      result = processTIMER_Delete(req);
      break;

    case VNSI_TIMER_UPDATE:
      result = processTIMER_Update(req);
      break;


    /** OPCODE 100 - 119: VNSI network functions for recording access */
    case VNSI_RECORDINGS_DISKSIZE:
      result = processRECORDINGS_GetDiskSpace(req);
      break;

    case VNSI_RECORDINGS_GETCOUNT:
      result = processRECORDINGS_GetCount(req);
      break;

    case VNSI_RECORDINGS_GETLIST:
      result = processRECORDINGS_GetList(req);
      break;

    case VNSI_RECORDINGS_RENAME:
      result = processRECORDINGS_Rename(req);
      break;

    case VNSI_RECORDINGS_DELETE:
      result = processRECORDINGS_Delete(req);
      break;

    case VNSI_RECORDINGS_GETEDL:
      result = processRECORDINGS_GetEdl(req);
      break;


    /** OPCODE 120 - 139: VNSI network functions for epg access and manipulating */
    case VNSI_EPG_GETFORCHANNEL:
      result = processEPG_GetForChannel(req);
      break;


    /** OPCODE 140 - 159: VNSI network functions for channel scanning */
    case VNSI_SCAN_SUPPORTED:
      result = processSCAN_ScanSupported(req);
      break;

    case VNSI_SCAN_GETCOUNTRIES:
      result = processSCAN_GetCountries(req);
      break;

    case VNSI_SCAN_GETSATELLITES:
      result = processSCAN_GetSatellites(req);
      break;

    case VNSI_SCAN_START:
      result = processSCAN_Start(req);
      break;

    case VNSI_SCAN_STOP:
      result = processSCAN_Stop(req);
      break;

    case VNSI_SCAN_SUPPORTED_TYPES:
      result = processSCAN_GetSupportedTypes(req);
      break;

    /** OPCODE 160 - 179: VNSI network functions for OSD */
    case VNSI_OSD_CONNECT:
      result = processOSD_Connect(req);
      break;

    case VNSI_OSD_DISCONNECT:
      result = processOSD_Disconnect();
      break;

    case VNSI_OSD_HITKEY:
      result = processOSD_Hitkey(req);
      break;


    /** OPCODE 180 - 189: VNSI network functions for deleted recording access */
    case VNSI_RECORDINGS_DELETED_ACCESS_SUPPORTED:
      result = processRECORDINGS_DELETED_Supported(req);
      break;

    case VNSI_RECORDINGS_DELETED_GETCOUNT:
      result = processRECORDINGS_DELETED_GetCount(req);
      break;

    case VNSI_RECORDINGS_DELETED_GETLIST:
      result = processRECORDINGS_DELETED_GetList(req);
      break;

    case VNSI_RECORDINGS_DELETED_DELETE:
      result = processRECORDINGS_DELETED_Delete(req);
      break;

    case VNSI_RECORDINGS_DELETED_UNDELETE:
      result = processRECORDINGS_DELETED_Undelete(req);
      break;

    case VNSI_RECORDINGS_DELETED_DELETE_ALL:
      result = processRECORDINGS_DELETED_DeleteAll(req);
      break;
  }

  return result;
}


/** OPCODE 1 - 19: VNSI network functions for general purpose */

bool cVNSIClient::process_Login(cRequestPacket &req) /* OPCODE 1 */
{
  if (req.getDataLength() <= 4) return false;

  m_protocolVersion      = req.extract_U32();
                           req.extract_U8();
  const char *clientName = req.extract_String();

  INFOLOG("Welcome client '%s' with protocol version '%u'", clientName, m_protocolVersion);

  // Send the login reply
  time_t timeNow        = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset        = timeStruct->tm_gmtoff;

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(VNSI_PROTOCOLVERSION);
  resp.add_U32(timeNow);
  resp.add_S32(timeOffset);
  resp.add_String("VDR-Network-Streaming-Interface (VNSI) Server");
  resp.add_String(VNSI_SERVER_VERSION);
  resp.finalise();

  if (m_protocolVersion < VNSI_MIN_PROTOCOLVERSION)
    ERRORLOG("Client '%s' have a not allowed protocol version '%u', terminating client", clientName, m_protocolVersion);
  else
    SetLoggedIn(true);

  if (m_protocolVersion < VNSI_RDS_PROTOCOLVERSION)
  {
    INFOLOG("RDS not supported on client '%s' and stream type disabled", clientName);
    m_bSupportRDS = false;
  }
  else
  {
    m_bSupportRDS = true;
  }

  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::process_GetTime(cRequestPacket &req) /* OPCODE 2 */
{
  time_t timeNow        = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset        = timeStruct->tm_gmtoff;

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(timeNow);
  resp.add_S32(timeOffset);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::process_EnableStatusInterface(cRequestPacket &req)
{
  bool enabled = req.extract_U8();

  SetStatusInterface(enabled);
  SetPriority(1);

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(VNSI_RET_OK);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::process_Ping(cRequestPacket &req) /* OPCODE 7 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(1);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::process_GetSetup(cRequestPacket &req) /* OPCODE 8 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());
  char* name = req.extract_String();
  if (!strcasecmp(name, CONFNAME_PMTTIMEOUT))
    resp.add_U32(PmtTimeout);
  else if (!strcasecmp(name, CONFNAME_TIMESHIFT))
    resp.add_U32(TimeshiftMode);
  else if (!strcasecmp(name, CONFNAME_TIMESHIFTBUFFERSIZE))
    resp.add_U32(TimeshiftBufferSize);
  else if (!strcasecmp(name, CONFNAME_TIMESHIFTBUFFERFILESIZE))
    resp.add_U32(TimeshiftBufferFileSize);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::process_StoreSetup(cRequestPacket &req) /* OPCODE 9 */
{
  char* name = req.extract_String();

  if (!strcasecmp(name, CONFNAME_PMTTIMEOUT))
  {
    int value = req.extract_U32();
    cPluginVNSIServer::StoreSetup(CONFNAME_PMTTIMEOUT, value);
  }
  else if (!strcasecmp(name, CONFNAME_TIMESHIFT))
  {
    int value = req.extract_U32();
    cPluginVNSIServer::StoreSetup(CONFNAME_TIMESHIFT, value);
  }
  else if (!strcasecmp(name, CONFNAME_TIMESHIFTBUFFERSIZE))
  {
    int value = req.extract_U32();
    cPluginVNSIServer::StoreSetup(CONFNAME_TIMESHIFTBUFFERSIZE, value);
  }
  else if (!strcasecmp(name, CONFNAME_TIMESHIFTBUFFERFILESIZE))
  {
    int value = req.extract_U32();
    cPluginVNSIServer::StoreSetup(CONFNAME_TIMESHIFTBUFFERFILESIZE, value);
  }
  else if (!strcasecmp(name, CONFNAME_PLAYRECORDING))
  {
    int value = req.extract_U32();
    cPluginVNSIServer::StoreSetup(CONFNAME_PLAYRECORDING, value);
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(VNSI_RET_OK);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

/** OPCODE 20 - 39: VNSI network functions for live streaming */

bool cVNSIClient::processChannelStream_Open(cRequestPacket &req) /* OPCODE 20 */
{
  uint32_t uid = req.extract_U32();
  int32_t priority = req.extract_S32();
  uint8_t timeshift = req.extract_U8();
  uint32_t timeout = req.extract_U32();

  if(timeout == 0)
    timeout = VNSIServerConfig.stream_timeout;

  if (m_isStreaming)
    StopChannelStreaming();

  const cChannel *channel = FindChannelByUID(uid);

  // try channelnumber
  if (channel == NULL)
  {
#if VDRVERSNUM >= 20301
    LOCK_CHANNELS_READ;
    channel = Channels->GetByNumber(uid);
#else
    channel = Channels.GetByNumber(uid);
#endif
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (channel == NULL) {
    ERRORLOG("Can't find channel %08x", uid);
    resp.add_U32(VNSI_RET_DATAINVALID);
  }
  else
  {
    if (StartChannelStreaming(resp, channel, priority, timeshift, timeout))
    {
      INFOLOG("Started streaming of channel %s (timeout %i seconds)", channel->Name(), timeout);
      // return here without sending the response
      // (was already done in cLiveStreamer::StreamChannel)
      return true;
    }

    DEBUGLOG("Can't stream channel %s", channel->Name());
    resp.add_U32(VNSI_RET_DATALOCKED);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return false;
}

bool cVNSIClient::processChannelStream_Close() /* OPCODE 21 */
{
  if (m_isStreaming)
    StopChannelStreaming();

  return true;
}

bool cVNSIClient::processChannelStream_Seek(cRequestPacket &req) /* OPCODE 22 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

  uint32_t serial = 0;
  if (m_isStreaming && m_Streamer)
  {
    int64_t time = req.extract_S64();
    if (m_Streamer->SeekTime(time, serial))
      resp.add_U32(VNSI_RET_OK);
    else
      resp.add_U32(VNSI_RET_ERROR);
  }
  else
    resp.add_U32(VNSI_RET_ERROR);

  resp.add_U32(serial);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

/** OPCODE 40 - 59: VNSI network functions for recording streaming */

bool cVNSIClient::processRecStream_Open(cRequestPacket &req) /* OPCODE 40 */
{
  const cRecording *recording = NULL;

  uint32_t uid = req.extract_U32();
  recording = cRecordingsCache::GetInstance().Lookup(uid);

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (recording && m_RecPlayer == NULL)
  {
    m_RecPlayer = new cRecPlayer(recording);

    resp.add_U32(VNSI_RET_OK);
    resp.add_U32(m_RecPlayer->getLengthFrames());
    resp.add_U64(m_RecPlayer->getLengthBytes());

#if VDRVERSNUM < 10703
    resp.add_U8(true);//added for TS
#else
    resp.add_U8(recording->IsPesRecording());//added for TS
#endif
  }
  else
  {
    resp.add_U32(VNSI_RET_DATAUNKNOWN);
    ERRORLOG("%s - unable to start recording !", __FUNCTION__);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::processRecStream_Close(cRequestPacket &req) /* OPCODE 41 */
{
  if (m_RecPlayer)
  {
    delete m_RecPlayer;
    m_RecPlayer = NULL;
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(VNSI_RET_OK);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRecStream_GetBlock(cRequestPacket &req) /* OPCODE 42 */
{
  if (m_isStreaming)
  {
    ERRORLOG("Get block called during live streaming");
    return false;
  }

  if (!m_RecPlayer)
  {
    ERRORLOG("Get block called when no recording open");
    return false;
  }

  uint64_t position  = req.extract_U64();
  uint32_t amount    = req.extract_U32();

  cResponsePacket resp;
  resp.init(req.getRequestID());

  uint8_t* p = resp.reserve(amount);
  uint32_t amountReceived = m_RecPlayer->getBlock(p, position, amount);

  if(amount > amountReceived) resp.unreserve(amount - amountReceived);

  if (!amountReceived)
  {
    resp.add_U32(0);
    DEBUGLOG("written 4(0) as getblock got 0");
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRecStream_PositionFromFrameNumber(cRequestPacket &req) /* OPCODE 43 */
{
  uint64_t retval       = 0;
  uint32_t frameNumber  = req.extract_U32();

  if (m_RecPlayer)
    retval = m_RecPlayer->positionFromFrameNumber(frameNumber);

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U64(retval);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  DEBUGLOG("Wrote posFromFrameNum reply to client");
  return true;
}

bool cVNSIClient::processRecStream_FrameNumberFromPosition(cRequestPacket &req) /* OPCODE 44 */
{
  uint32_t retval   = 0;
  uint64_t position = req.extract_U64();

  if (m_RecPlayer)
    retval = m_RecPlayer->frameNumberFromPosition(position);

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(retval);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  DEBUGLOG("Wrote frameNumFromPos reply to client");
  return true;
}

bool cVNSIClient::processRecStream_GetIFrame(cRequestPacket &req) /* OPCODE 45 */
{
  bool success            = false;
  uint32_t frameNumber    = req.extract_U32();
  uint32_t direction      = req.extract_U32();
  uint64_t rfilePosition  = 0;
  uint32_t rframeNumber   = 0;
  uint32_t rframeLength   = 0;

  if (m_RecPlayer)
    success = m_RecPlayer->getNextIFrame(frameNumber, direction, &rfilePosition, &rframeNumber, &rframeLength);

  cResponsePacket resp;
  resp.init(req.getRequestID());

  // returns file position, frame number, length
  if (success)
  {
    resp.add_U64(rfilePosition);
    resp.add_U32(rframeNumber);
    resp.add_U32(rframeLength);
  }
  else
  {
    resp.add_U32(0);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  DEBUGLOG("Wrote GNIF reply to client %lu %u %u", rfilePosition, rframeNumber, rframeLength);
  return true;
}

bool cVNSIClient::processRecStream_GetLength(cRequestPacket &req) /* OPCODE 46 */
{
  uint64_t length = 0;

  if (m_RecPlayer)
  {
    m_RecPlayer->reScan();
    length = m_RecPlayer->getLengthBytes();
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U64(length);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

/** OPCODE 60 - 79: VNSI network functions for channel access */

bool cVNSIClient::processCHANNELS_ChannelsCount(cRequestPacket &req) /* OPCODE 61 */
{
  int count = 0;
#if VDRVERSNUM >= 20301
  {
    LOCK_CHANNELS_READ;
    count = Channels->MaxNumber();
  }
#else
  Channels.Lock(false);
  count = Channels.MaxNumber();
  Channels.Unlock();
#endif

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(count);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_GetChannels(cRequestPacket &req) /* OPCODE 63 */
{
  if (req.getDataLength() != 5) return false;

  bool radio = req.extract_U32();
  bool filter = req.extract_U8();

#if VDRVERSNUM >= 20301
  cStateKey ChannelsKey(true);
  const cChannels *Channels = cChannels::GetChannelsRead(ChannelsKey);
#else
  Channels.Lock(false);
#endif

  cResponsePacket resp;
  resp.init(req.getRequestID());

  cString caids;
  int caid;
  int caid_idx;
#if VDRVERSNUM >= 20301
  for (const cChannel *channel = Channels->First(); channel; channel = Channels->Next(channel))
#else
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
#endif
  {
    if (radio != cVNSIChannelFilter::IsRadio(channel))
      continue;

    // skip invalid channels
    if (channel->Sid() == 0)
      continue;

    // check filter
    if (filter && !VNSIChannelFilter.PassFilter(*channel))
      continue;

    uint32_t uuid = CreateChannelUID(channel);
    resp.add_U32(channel->Number());
    resp.add_String(m_toUTF8.Convert(channel->Name()));
    resp.add_String(m_toUTF8.Convert(channel->Provider()));
    resp.add_U32(uuid);
    resp.add_U32(channel->Ca(0));
    caid_idx = 0;
    caids = "caids:";
    while((caid = channel->Ca(caid_idx)) != 0)
    {
      caids = cString::sprintf("%s%d;", (const char*)caids, caid);
      caid_idx++;
    }
    resp.add_String((const char*)caids);
    if (m_protocolVersion >= 6)
    {
      resp.add_String(CreatePiconRef(channel));
    }

    // create entry in EPG map on first query
    std::map<int, sEpgUpdate>::iterator it;
    it = m_epgUpdate.find(uuid);
    if (it == m_epgUpdate.end())
    {
      m_epgUpdate[uuid].lastEvent = 0;
      m_epgUpdate[uuid].attempts = 0;
    }
  }

#if VDRVERSNUM >= 20301
  ChannelsKey.Remove();
#else
  Channels.Unlock();
#endif

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::processCHANNELS_GroupsCount(cRequestPacket &req)
{
  uint32_t type = req.extract_U32();

  m_channelgroups[0].clear();
  m_channelgroups[1].clear();

  switch(type)
  {
    // get groups defined in channels.conf
    default:
    case 0:
      CreateChannelGroups(false);
      break;
    // automatically create groups
    case 1:
      CreateChannelGroups(true);
      break;
  }

  uint32_t count = m_channelgroups[0].size() + m_channelgroups[1].size();

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(count);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_GroupList(cRequestPacket &req)
{
  uint32_t radio = req.extract_U8();
  std::map<std::string, ChannelGroup>::iterator i;

  cResponsePacket resp;
  resp.init(req.getRequestID());

  for(i = m_channelgroups[radio].begin(); i != m_channelgroups[radio].end(); i++)
  {
    resp.add_String(i->second.name.c_str());
    resp.add_U8(i->second.radio);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_GetGroupMembers(cRequestPacket &req)
{
  char* groupname = req.extract_String();
  uint32_t radio = req.extract_U8();
  bool filter = req.extract_U8();
  int index = 0;

  cResponsePacket resp;
  resp.init(req.getRequestID());

  // unknown group
  if(m_channelgroups[radio].find(groupname) == m_channelgroups[radio].end())
  {
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
    return true;
  }

  bool automatic = m_channelgroups[radio][groupname].automatic;
  std::string name;

#if VDRVERSNUM >= 20301
  cStateKey ChannelsKey(true);
  const cChannels *Channels = cChannels::GetChannelsRead(ChannelsKey);
  for (const cChannel *channel = Channels->First(); channel; channel = Channels->Next(channel))
#else
  Channels.Lock(false);
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
#endif
  {

    if(automatic && !channel->GroupSep())
      name = channel->Provider();
    else
    {
      if(channel->GroupSep())
      {
        name = channel->Name();
        continue;
      }
    }

    if(name.empty())
      continue;

    if(cVNSIChannelFilter::IsRadio(channel) != radio)
      continue;

    // check filter
    if (filter && !VNSIChannelFilter.PassFilter(*channel))
      continue;

    if(name == groupname)
    {
      resp.add_U32(CreateChannelUID(channel));
      resp.add_U32(++index);
    }
  }

#if VDRVERSNUM >= 20301
  ChannelsKey.Remove();
#else
  Channels.Unlock();
#endif

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_GetCaids(cRequestPacket &req)
{
  uint32_t uid = req.extract_U32();

  const cChannel *channel = FindChannelByUID(uid);

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (channel != NULL)
  {
    int caid;
    int idx = 0;
    while((caid = channel->Ca(idx)) != 0)
    {
      resp.add_U32(caid);
      idx++;
    }
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::processCHANNELS_GetWhitelist(cRequestPacket &req)
{
  bool radio = req.extract_U8();
  std::vector<cVNSIProvider> *providers;

  if(radio)
    providers = &VNSIChannelFilter.m_providersRadio;
  else
    providers = &VNSIChannelFilter.m_providersVideo;

  cResponsePacket resp;
  resp.init(req.getRequestID());

  VNSIChannelFilter.m_Mutex.Lock();
  for(unsigned int i=0; i<providers->size(); i++)
  {
    resp.add_String((*providers)[i].m_name.c_str());
    resp.add_U32((*providers)[i].m_caid);
  }
  VNSIChannelFilter.m_Mutex.Unlock();

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_GetBlacklist(cRequestPacket &req)
{
  bool radio = req.extract_U8();
  std::vector<int> *channels;

  if(radio)
    channels = &VNSIChannelFilter.m_channelsRadio;
  else
    channels = &VNSIChannelFilter.m_channelsVideo;

  cResponsePacket resp;
  resp.init(req.getRequestID());

  VNSIChannelFilter.m_Mutex.Lock();
  for(unsigned int i=0; i<channels->size(); i++)
  {
    resp.add_U32((*channels)[i]);
  }
  VNSIChannelFilter.m_Mutex.Unlock();

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_SetWhitelist(cRequestPacket &req)
{
  bool radio = req.extract_U8();
  cVNSIProvider provider;
  std::vector<cVNSIProvider> *providers;

  if(radio)
    providers = &VNSIChannelFilter.m_providersRadio;
  else
    providers = &VNSIChannelFilter.m_providersVideo;

  VNSIChannelFilter.m_Mutex.Lock();
  providers->clear();

  while(!req.end())
  {
    char *str = req.extract_String();
    provider.m_name = str;
    provider.m_caid = req.extract_U32();
    providers->push_back(provider);
  }
  VNSIChannelFilter.StoreWhitelist(radio);
  VNSIChannelFilter.m_Mutex.Unlock();

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processCHANNELS_SetBlacklist(cRequestPacket &req)
{
  bool radio = req.extract_U8();
  cVNSIProvider provider;
  std::vector<int> *channels;

  if(radio)
    channels = &VNSIChannelFilter.m_channelsRadio;
  else
    channels = &VNSIChannelFilter.m_channelsVideo;

  VNSIChannelFilter.m_Mutex.Lock();
  channels->clear();

  int id;
  while(!req.end())
  {
    id = req.extract_U32();
    channels->push_back(id);
  }
  VNSIChannelFilter.StoreBlacklist(radio);
  VNSIChannelFilter.m_Mutex.Unlock();

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

void cVNSIClient::CreateChannelGroups(bool automatic)
{
  std::string groupname;

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  for (const cChannel *channel = Channels->First(); channel; channel = Channels->Next(channel))
#else
  Channels.Lock(false);
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
#endif
  {
    bool isRadio = cVNSIChannelFilter::IsRadio(channel);

    if(automatic && !channel->GroupSep())
      groupname = channel->Provider();
    else if(!automatic && channel->GroupSep())
      groupname = channel->Name();

    if(groupname.empty())
      continue;

    if(m_channelgroups[isRadio].find(groupname) == m_channelgroups[isRadio].end())
    {
      ChannelGroup group;
      group.name = groupname;
      group.radio = isRadio;
      group.automatic = automatic;
      m_channelgroups[isRadio][groupname] = group;
    }
  }

#if VDRVERSNUM < 20301
  Channels.Unlock();
#endif
}

/** OPCODE 80 - 99: VNSI network functions for timer access */

bool cVNSIClient::processTIMER_GetCount(cRequestPacket &req) /* OPCODE 80 */
{
  cMutexLock lock(&m_timerLock);

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_READ;
  int count = Timers->Count();
#else
  int count = Timers.Count();
#endif

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(count);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processTIMER_Get(cRequestPacket &req) /* OPCODE 81 */
{
  cMutexLock lock(&m_timerLock);

  uint32_t number = req.extract_U32();

  cResponsePacket resp;
  resp.init(req.getRequestID());

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_READ;
  int numTimers = Timers->Count();
  if (numTimers > 0)
  {
    const cTimer *timer = Timers->Get(number-1);
#else
    int numTimers = Timers.Count();
    if (numTimers > 0)
    {
      cTimer *timer = Timers.Get(number-1);
#endif
      if (timer)
      {
        resp.add_U32(VNSI_RET_OK);

        resp.add_U32(timer->Index()+1);
        resp.add_U32(timer->HasFlags(tfActive));
        resp.add_U32(timer->Recording());
        resp.add_U32(timer->Pending());
        resp.add_U32(timer->Priority());
        resp.add_U32(timer->Lifetime());
        resp.add_U32(timer->Channel()->Number());
        resp.add_U32(CreateChannelUID(timer->Channel()));
        resp.add_U32(timer->StartTime());
        resp.add_U32(timer->StopTime());
        resp.add_U32(timer->Day());
        resp.add_U32(timer->WeekDays());
        resp.add_String(m_toUTF8.Convert(timer->File()));
      }
      else
        resp.add_U32(VNSI_RET_DATAUNKNOWN);
    }
    else
      resp.add_U32(VNSI_RET_DATAUNKNOWN);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processTIMER_GetList(cRequestPacket &req) /* OPCODE 82 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

  cMutexLock lock(&m_timerLock);

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_READ;
  int numTimers = Timers->Count();
  resp.add_U32(numTimers);
  for (int i = 0; i < numTimers; i++)
  {
    const cTimer *timer = Timers->Get(i);
#else
  int numTimers = Timers.Count();
  resp.add_U32(numTimers);
  for (int i = 0; i < numTimers; i++)
  {
    cTimer *timer = Timers.Get(i);
#endif
    if (!timer)
      continue;

    resp.add_U32(timer->Index()+1);
    resp.add_U32(timer->HasFlags(tfActive));
    resp.add_U32(timer->Recording());
    resp.add_U32(timer->Pending());
    resp.add_U32(timer->Priority());
    resp.add_U32(timer->Lifetime());
    resp.add_U32(timer->Channel()->Number());
    resp.add_U32(CreateChannelUID(timer->Channel()));
    resp.add_U32(timer->StartTime());
    resp.add_U32(timer->StopTime());
    resp.add_U32(timer->Day());
    resp.add_U32(timer->WeekDays());
    resp.add_String(m_toUTF8.Convert(timer->File()));
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processTIMER_Add(cRequestPacket &req) /* OPCODE 83 */
{
  cMutexLock lock(&m_timerLock);

  uint32_t flags      = req.extract_U32() > 0 ? tfActive : tfNone;
  uint32_t priority   = req.extract_U32();
  uint32_t lifetime   = req.extract_U32();
  uint32_t channelid  = req.extract_U32();
  time_t startTime    = req.extract_U32();
  time_t stopTime     = req.extract_U32();
  time_t day          = req.extract_U32();
  uint32_t weekdays   = req.extract_U32();
  const char *file    = req.extract_String();
  const char *aux     = req.extract_String();

  // handle instant timers
  if(startTime == -1 || startTime == 0)
  {
    startTime = time(NULL);
  }

  struct tm tm_r;
  struct tm *time = localtime_r(&startTime, &tm_r);
  if (day <= 0)
    day = cTimer::SetTime(startTime, 0);
  int start = time->tm_hour * 100 + time->tm_min;
  time = localtime_r(&stopTime, &tm_r);
  int stop = time->tm_hour * 100 + time->tm_min;

  cString buffer;
  const cChannel* channel = FindChannelByUID(channelid);
  if(channel != NULL)
  {
    buffer = cString::sprintf("%u:%s:%s:%04d:%04d:%d:%d:%s:%s\n", flags, (const char*)channel->GetChannelID().ToString(), *cTimer::PrintDay(day, weekdays, true), start, stop, priority, lifetime, file, aux);
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());

  cTimer *timer = new cTimer;
  if (timer->Parse(buffer))
  {
#if VDRVERSNUM >= 20301
    LOCK_TIMERS_WRITE;
    const cTimer *t = Timers->GetTimer(timer);
    if (!t)
    {
      Timers->Add(timer);
      Timers->SetModified();
      INFOLOG("Timer %s added", *timer->ToDescr());
      resp.add_U32(VNSI_RET_OK);
      resp.finalise();
      m_socket.write(resp.getPtr(), resp.getLen());
      return true;
    }
#else
    cTimer *t = Timers.GetTimer(timer);
    if (!t)
    {
      Timers.Add(timer);
      Timers.SetModified();
      INFOLOG("Timer %s added", *timer->ToDescr());
      resp.add_U32(VNSI_RET_OK);
      resp.finalise();
      m_socket.write(resp.getPtr(), resp.getLen());
      return true;
    }
#endif
    else
    {
      ERRORLOG("Timer already defined: %d %s", t->Index() + 1, *t->ToText());
      resp.add_U32(VNSI_RET_DATALOCKED);
    }
  }
  else
  {
    ERRORLOG("Error in timer settings");
    resp.add_U32(VNSI_RET_DATAINVALID);
  }

  delete timer;

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processTIMER_Delete(cRequestPacket &req) /* OPCODE 84 */
{
  cMutexLock lock(&m_timerLock);

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_WRITE;
  int timersCount = Timers->Count();
#else
  int timersCount = Timers.Count();
#endif

  uint32_t number = req.extract_U32();
  bool     force  = req.extract_U32();

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (number <= 0 || number > (uint32_t)timersCount)
  {
    ERRORLOG("Unable to delete timer - invalid timer identifier");
    resp.add_U32(VNSI_RET_DATAINVALID);
  }
  else
  {
#if VDRVERSNUM >= 20301
    cTimer *timer = Timers->Get(number-1);
    if (timer)
    {
      Timers->SetExplicitModify();
      {
        if (timer->Recording())
        {
          if (force)
          {
            timer->Skip();
            cRecordControls::Process(Timers, time(NULL));
          }
          else
          {
            ERRORLOG("Timer \"%i\" is recording and can be deleted (use force=1 to stop it)", number);
            resp.add_U32(VNSI_RET_RECRUNNING);
            resp.finalise();
            m_socket.write(resp.getPtr(), resp.getLen());
            return true;
          }
        }
        INFOLOG("Deleting timer %s", *timer->ToDescr());
        Timers->Del(timer);
        Timers->SetModified();
        resp.add_U32(VNSI_RET_OK);
      }
#else
    cTimer *timer = Timers.Get(number-1);
    if (timer)
    {
      if (!Timers.BeingEdited())
      {
        if (timer->Recording())
        {
          if (force)
          {
            timer->Skip();
            cRecordControls::Process(time(NULL));
          }
          else
          {
            ERRORLOG("Timer \"%i\" is recording and can be deleted (use force=1 to stop it)", number);
            resp.add_U32(VNSI_RET_RECRUNNING);
            resp.finalise();
            m_socket.write(resp.getPtr(), resp.getLen());
            return true;
          }
        }
        INFOLOG("Deleting timer %s", *timer->ToDescr());
        Timers.Del(timer);
        Timers.SetModified();
        resp.add_U32(VNSI_RET_OK);
      }
      else
      {
        ERRORLOG("Unable to delete timer - timers being edited at VDR");
        resp.add_U32(VNSI_RET_DATALOCKED);
      }
#endif
    }
    else
    {
      ERRORLOG("Unable to delete timer - invalid timer identifier");
      resp.add_U32(VNSI_RET_DATAINVALID);
    }
  }
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processTIMER_Update(cRequestPacket &req) /* OPCODE 85 */
{
  cMutexLock lock(&m_timerLock);

  int length      = req.getDataLength();
  uint32_t index  = req.extract_U32();
  bool active     = req.extract_U32();

#if VDRVERSNUM >= 20301
  LOCK_TIMERS_WRITE;
  cTimer *timer = Timers->Get(index - 1);
#else
  cTimer *timer = Timers.Get(index - 1);
#endif

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (!timer)
  {
    ERRORLOG("Timer \"%u\" not defined", index);
    resp.add_U32(VNSI_RET_DATAUNKNOWN);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
    return true;
  }

  cTimer t = *timer;

  if (length == 8)
  {
    if (active)
      t.SetFlags(tfActive);
    else
      t.ClrFlags(tfActive);
  }
  else
  {
    uint32_t flags      = active ? tfActive : tfNone;
    uint32_t priority   = req.extract_U32();
    uint32_t lifetime   = req.extract_U32();
    uint32_t channelid  = req.extract_U32();
    time_t startTime    = req.extract_U32();
    time_t stopTime     = req.extract_U32();
    time_t day          = req.extract_U32();
    uint32_t weekdays   = req.extract_U32();
    const char *file    = req.extract_String();
    const char *aux     = req.extract_String();

    struct tm tm_r;
    struct tm *time = localtime_r(&startTime, &tm_r);
    if (day <= 0)
      day = cTimer::SetTime(startTime, 0);
    int start = time->tm_hour * 100 + time->tm_min;
    time = localtime_r(&stopTime, &tm_r);
    int stop = time->tm_hour * 100 + time->tm_min;

    cString buffer;
    const cChannel* channel = FindChannelByUID(channelid);
    if(channel != NULL)
    {
      buffer = cString::sprintf("%u:%s:%s:%04d:%04d:%d:%d:%s:%s\n", flags, (const char*)channel->GetChannelID().ToString(), *cTimer::PrintDay(day, weekdays, true), start, stop, priority, lifetime, file, aux);
    }

    if (!t.Parse(buffer))
    {
      ERRORLOG("Error in timer settings");
      resp.add_U32(VNSI_RET_DATAINVALID);
      resp.finalise();
      m_socket.write(resp.getPtr(), resp.getLen());
      return true;
    }
  }

  *timer = t;
#if VDRVERSNUM >= 20301
  Timers->SetModified();
#else
  Timers.SetModified();
#endif

  resp.add_U32(VNSI_RET_OK);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}


/** OPCODE 100 - 119: VNSI network functions for recording access */

bool cVNSIClient::processRECORDINGS_GetDiskSpace(cRequestPacket &req) /* OPCODE 100 */
{
  int FreeMB;
  int UsedMB;
#if VDRVERSNUM >= 20102
  int Percent = cVideoDirectory::VideoDiskSpace(&FreeMB, &UsedMB);
#else
  int Percent = VideoDiskSpace(&FreeMB, &UsedMB);
#endif
  int Total = FreeMB + UsedMB;

  cResponsePacket resp;
  resp.init(req.getRequestID());

  resp.add_U32(Total);
  resp.add_U32(FreeMB);
  resp.add_U32(Percent);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_GetCount(cRequestPacket &req) /* OPCODE 101 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
  resp.add_U32(Recordings->Count());
#else
  resp.add_U32(Recordings.Count());
#endif

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_GetList(cRequestPacket &req) /* OPCODE 102 */
{
  cMutexLock lock(&m_timerLock);
#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_READ;
#else
  cThreadLock RecordingsLock(&Recordings);
#endif

  cResponsePacket resp;
  resp.init(req.getRequestID());

#if VDRVERSNUM >= 20301
  for (const cRecording *recording = Recordings->First(); recording; recording = Recordings->Next(recording))
#else
  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
#endif
  {
#if APIVERSNUM >= 10705
    const cEvent *event = recording->Info()->GetEvent();
#else
    const cEvent *event = NULL;
#endif

    time_t recordingStart    = 0;
    int    recordingDuration = 0;
    if (event)
    {
      recordingStart    = event->StartTime();
      recordingDuration = event->Duration();
    }
    else
    {
      cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
      if (rc)
      {
        recordingStart    = rc->Timer()->StartTime();
        recordingDuration = rc->Timer()->StopTime() - recordingStart;
      }
      else
      {
#if APIVERSNUM >= 10727
        recordingStart = recording->Start();
#else
        recordingStart = recording->start;
#endif
      }
    }
    DEBUGLOG("GRI: RC: recordingStart=%lu recordingDuration=%i", recordingStart, recordingDuration);

    // recording_time
    resp.add_U32(recordingStart);

    // duration
    resp.add_U32(recordingDuration);

    // priority
#if APIVERSNUM >= 10727
    resp.add_U32(recording->Priority());
#else
    resp.add_U32(recording->priority);
#endif

    // lifetime
#if APIVERSNUM >= 10727
    resp.add_U32(recording->Lifetime());
#else
    resp.add_U32(recording->lifetime);
#endif

    // channel_name
    resp.add_String(recording->Info()->ChannelName() ? m_toUTF8.Convert(recording->Info()->ChannelName()) : "");

    char* fullname = strdup(recording->Name());
    char* recname = strrchr(fullname, FOLDERDELIMCHAR);
    char* directory = NULL;

    if(recname == NULL) {
      recname = fullname;
    }
    else {
      *recname = 0;
      recname++;
      directory = fullname;
    }

    // title
    resp.add_String(m_toUTF8.Convert(recname));

    // subtitle
    if (!isempty(recording->Info()->ShortText()))
      resp.add_String(m_toUTF8.Convert(recording->Info()->ShortText()));
    else
      resp.add_String("");

    // description
    if (!isempty(recording->Info()->Description()))
      resp.add_String(m_toUTF8.Convert(recording->Info()->Description()));
    else
      resp.add_String("");

    // directory
    if(directory != NULL) {
      char* p = directory;
      while(*p != 0) {
        if(*p == FOLDERDELIMCHAR) *p = '/';
        if(*p == '_') *p = ' ';
        p++;
      }
      while(*directory == '/') directory++;
    }

    resp.add_String((isempty(directory)) ? "" : m_toUTF8.Convert(directory));

    // filename / uid of recording
    uint32_t uid = cRecordingsCache::GetInstance().Register(recording, false);
    resp.add_U32(uid);

    free(fullname);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_Rename(cRequestPacket &req) /* OPCODE 103 */
{
  uint32_t uid = req.extract_U32();
  char* newtitle = req.extract_String();
  int r = VNSI_RET_DATAINVALID;

#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_WRITE;
#endif

  const cRecording* recording = cRecordingsCache::GetInstance().Lookup(uid);

  if(recording != NULL) {
    // get filename and remove last part (recording time)
    char* filename_old = strdup((const char*)recording->FileName());
    char* sep = strrchr(filename_old, '/');
    if(sep != NULL) {
      *sep = 0;
    }

    // replace spaces in newtitle
    strreplace(newtitle, ' ', '_');
    char* filename_new = new char[1024];
    strncpy(filename_new, filename_old, 512);
    sep = strrchr(filename_new, '/');
    if(sep != NULL) {
      sep++;
      *sep = 0;
    }
    strncat(filename_new, newtitle, 512);

    INFOLOG("renaming recording '%s' to '%s'", filename_old, filename_new);
    r = rename(filename_old, filename_new);

#if VDRVERSNUM >= 20301
    Recordings->Update();
#else
    Recordings.Update();
#endif

    free(filename_old);
    delete[] filename_new;
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(r);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::processRECORDINGS_Delete(cRequestPacket &req) /* OPCODE 104 */
{
  cString recName;
  cRecording* recording = NULL;

#if VDRVERSNUM >= 20301
  LOCK_RECORDINGS_WRITE;
#endif

  uint32_t uid = req.extract_U32();
  recording = cRecordingsCache::GetInstance().LookupWrite(uid);

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (recording)
  {
    DEBUGLOG("deleting recording: %s", recording->Name());

    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      if (recording->Delete())
      {
        // Copy svdrdeveldevelp's way of doing this, see if it works
#if VDRVERSNUM >= 20301
        Recordings->DelByName(recording->FileName());
#else
        Recordings.DelByName(recording->FileName());
#endif
        INFOLOG("Recording \"%s\" deleted", recording->FileName());
        resp.add_U32(VNSI_RET_OK);
      }
      else
      {
        ERRORLOG("Error while deleting recording!");
        resp.add_U32(VNSI_RET_ERROR);
      }
    }
    else
    {
      ERRORLOG("Recording \"%s\" is in use by timer %d", recording->Name(), rc->Timer()->Index() + 1);
      resp.add_U32(VNSI_RET_DATALOCKED);
    }
  }
  else
  {
    ERRORLOG("Error in recording name \"%s\"", (const char*)recName);
    resp.add_U32(VNSI_RET_DATAUNKNOWN);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::processRECORDINGS_GetEdl(cRequestPacket &req) /* OPCODE 105 */
{
  cString recName;
  const cRecording* recording = NULL;

  uint32_t uid = req.extract_U32();
  recording = cRecordingsCache::GetInstance().Lookup(uid);

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (recording)
  {
    cMarks marks;
    if(marks.Load(recording->FileName(), recording->FramesPerSecond(), recording->IsPesRecording()))
    {
#if VDRVERSNUM >= 10732
      cMark* mark = NULL;
      double fps = recording->FramesPerSecond();
      while((mark = marks.GetNextBegin(mark)) != NULL)
      {
        resp.add_U64(mark->Position() *1000 / fps);
        resp.add_U64(mark->Position() *1000 / fps);
        resp.add_S32(2);
      }
#endif
    }
  }
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}


/** OPCODE 120 - 139: VNSI network functions for epg access and manipulating */

bool cVNSIClient::processEPG_GetForChannel(cRequestPacket &req) /* OPCODE 120 */
{
  uint32_t channelUID  = 0;

  channelUID = req.extract_U32();

  uint32_t startTime      = req.extract_U32();
  uint32_t duration       = req.extract_U32();

#if VDRVERSNUM >= 20301
  LOCK_CHANNELS_READ;
  LOCK_SCHEDULES_READ;
#else
  Channels.Lock(false);
#endif

  const cChannel* channel = NULL;

  channel = FindChannelByUID(channelUID);
  if(channel != NULL)
  {
    DEBUGLOG("get schedule called for channel '%s'", (const char*)channel->GetChannelID().ToString());
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (!channel)
  {
    resp.add_U32(0);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
#if VDRVERSNUM < 20301
    Channels.Unlock();
#endif

    ERRORLOG("written 0 because channel = NULL");
    return true;
  }

#if VDRVERSNUM < 20301
  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
  if (!Schedules)
  {
    resp.add_U32(0);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
    Channels.Unlock();

    DEBUGLOG("written 0 because Schedule!s! = NULL");
    return true;
  }
#endif

#if VDRVERSNUM >= 20301
  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
#else
  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
#endif
  if (!Schedule)
  {
    resp.add_U32(0);
    resp.finalise();
    m_socket.write(resp.getPtr(), resp.getLen());
#if VDRVERSNUM < 20301
    Channels.Unlock();
#endif

    DEBUGLOG("written 0 because Schedule = NULL");
    return true;
  }

  bool atLeastOneEvent = false;

  uint32_t thisEventID;
  uint32_t thisEventTime;
  uint32_t thisEventDuration;
  uint32_t thisEventContent;
  uint32_t thisEventRating;
  const char* thisEventTitle;
  const char* thisEventSubTitle;
  const char* thisEventDescription;

  for (const cEvent* event = Schedule->Events()->First(); event; event = Schedule->Events()->Next(event))
  {
    thisEventID           = event->EventID();
    thisEventTitle        = event->Title();
    thisEventSubTitle     = event->ShortText();
    thisEventDescription  = event->Description();
    thisEventTime         = event->StartTime();
    thisEventDuration     = event->Duration();
#if defined(USE_PARENTALRATING) || defined(PARENTALRATINGCONTENTVERSNUM)
    thisEventContent      = event->Contents();
    thisEventRating       = 0;
#elif APIVERSNUM >= 10711
    thisEventContent      = event->Contents();
    thisEventRating       = event->ParentalRating();
#else
    thisEventContent      = 0;
    thisEventRating       = 0;
#endif

    //in the past filter
    if ((thisEventTime + thisEventDuration) < (uint32_t)time(NULL)) continue;

    //start time filter
    if ((thisEventTime + thisEventDuration) <= startTime) continue;

    //duration filter
    if (duration != 0 && thisEventTime >= (startTime + duration)) continue;

    if (!thisEventTitle)        thisEventTitle        = "";
    if (!thisEventSubTitle)     thisEventSubTitle     = "";
    if (!thisEventDescription)  thisEventDescription  = "";

    resp.add_U32(thisEventID);
    resp.add_U32(thisEventTime);
    resp.add_U32(thisEventDuration);
    resp.add_U32(thisEventContent);
    resp.add_U32(thisEventRating);

    resp.add_String(m_toUTF8.Convert(thisEventTitle));
    resp.add_String(m_toUTF8.Convert(thisEventSubTitle));
    resp.add_String(m_toUTF8.Convert(thisEventDescription));

    atLeastOneEvent = true;
  }

#if VDRVERSNUM < 20301
  Channels.Unlock();
#endif

  DEBUGLOG("Got all event data");

  if (!atLeastOneEvent)
  {
    resp.add_U32(0);
    DEBUGLOG("Written 0 because no data");
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  const cEvent *lastEvent =  Schedule->Events()->Last();
  if (lastEvent)
  {
    m_epgUpdate[channelUID].lastEvent = lastEvent->StartTime();
    m_epgUpdate[channelUID].attempts = 0;
  }
  DEBUGLOG("written schedules packet");

  return true;
}


/*!
 * OPCODE 140 - 169:
 * VNSI network functions for channel scanning
 */

bool cVNSIClient::processSCAN_ScanSupported(cRequestPacket &req) /* OPCODE 140 */
{
  uint32_t retValue = VNSI_RET_NOTSUPPORTED;
  if (!m_inhibidDataUpdates && m_ChannelScanControl.IsSupported())
    retValue = VNSI_RET_OK;

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(retValue);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processSCAN_GetSupportedTypes(cRequestPacket &req)
{
  uint32_t retValue = 0;
  if (m_ChannelScanControl.IsSupported())
  {
    retValue |= m_ChannelScanControl.SupportsDVB_T()        ? VNSI_SCAN_SUPPORT_DVB_T : 0;
    retValue |= m_ChannelScanControl.SupportsDVB_C()        ? VNSI_SCAN_SUPPORT_DVB_C : 0;
    retValue |= m_ChannelScanControl.SupportsDVB_S()        ? VNSI_SCAN_SUPPORT_DVB_S : 0;
    retValue |= m_ChannelScanControl.SupportsAnalogTV()     ? VNSI_SCAN_SUPPORT_ANALOG_TV : 0;
    retValue |= m_ChannelScanControl.SupportsAnalogRadio()  ? VNSI_SCAN_SUPPORT_ANALOG_RADIO : 0;
    retValue |= m_ChannelScanControl.SupportsATSC()         ? VNSI_SCAN_SUPPORT_ATSC : 0;
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(retValue);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processSCAN_GetCountries(cRequestPacket &req) /* OPCODE 141 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

  scannerEntryList list;
  if (m_ChannelScanControl.GetCountries(list))
  {
    resp.add_U32(VNSI_RET_OK);
    for (scannerEntryList::const_iterator it = list.begin(); it != list.end(); ++it)
    {
      resp.add_U32(it->index);
      resp.add_String(it->name);
      resp.add_String(it->longName);
    }
  }
  else
  {
    resp.add_U32(VNSI_RET_NOTSUPPORTED);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processSCAN_GetSatellites(cRequestPacket &req) /* OPCODE 142 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

  scannerEntryList list;
  if (m_ChannelScanControl.GetSatellites(list))
  {
    resp.add_U32(VNSI_RET_OK);
    for (scannerEntryList::const_iterator it = list.begin(); it != list.end(); ++it)
    {
      resp.add_U32(it->index);
      resp.add_String(it->name);
      resp.add_String(it->longName);
    }
  }
  else
  {
    resp.add_U32(VNSI_RET_NOTSUPPORTED);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processSCAN_Start(cRequestPacket &req) /* OPCODE 143 */
{
  sScanServiceData svc;
  svc.type              = (int)req.extract_U32();
  svc.scan_tv           = (bool)req.extract_U8();
  svc.scan_radio        = (bool)req.extract_U8();
  svc.scan_fta          = (bool)req.extract_U8();
  svc.scan_scrambled    = (bool)req.extract_U8();
  svc.scan_hd           = (bool)req.extract_U8();
  svc.CountryIndex      = (int)req.extract_U32();
  svc.DVBC_Inversion    = (int)req.extract_U32();
  svc.DVBC_Symbolrate   = (int)req.extract_U32();
  svc.DVBC_QAM          = (int)req.extract_U32();
  svc.DVBT_Inversion    = (int)req.extract_U32();
  svc.SatIndex          = (int)req.extract_U32();
  svc.ATSC_Type         = (int)req.extract_U32();

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (!m_inhibidDataUpdates && m_ChannelScanControl.IsSupported())
  {
    if (m_ChannelScanControl.StartScan(svc))
    {
      resp.add_U32(VNSI_RET_OK);
      m_inhibidDataUpdates = true;
    }
    else
      resp.add_U32(VNSI_RET_ERROR);
  }
  else
    resp.add_U32(VNSI_RET_NOTSUPPORTED);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processSCAN_Stop(cRequestPacket &req) /* OPCODE 144 */
{
  m_inhibidDataUpdates = false;

  cResponsePacket resp;
  resp.init(req.getRequestID());

  if (m_ChannelScanControl.IsSupported())
  {
    if (m_ChannelScanControl.StopScan())
      resp.add_U32(VNSI_RET_OK);
    else
      resp.add_U32(VNSI_RET_ERROR);
  }
  else
    resp.add_U32(VNSI_RET_NOTSUPPORTED);

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

void cVNSIClient::processSCAN_SetPercentage(int percent)
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_PERCENTAGE);
  resp.add_U32(percent);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::processSCAN_SetSignalStrength(int strength, bool locked)
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_SIGNAL);
  resp.add_U32(strength);
  resp.add_U32(locked);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::processSCAN_SetDeviceInfo(const char *Info)
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_DEVICE);
  resp.add_String(Info);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::processSCAN_SetTransponder(const char *Info)
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_TRANSPONDER);
  resp.add_String(Info);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::processSCAN_NewChannel(const char *Name, bool isRadio, bool isEncrypted, bool isHD)
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_NEWCHANNEL);
  resp.add_U32(isRadio);
  resp.add_U32(isEncrypted);
  resp.add_U32(isHD);
  resp.add_String(Name);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::processSCAN_IsFinished()
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_FINISHED);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

void cVNSIClient::processSCAN_SetStatus(int status)
{
  cResponsePacket resp;
  resp.initScan(VNSI_SCANNER_STATUS);
  resp.add_U32(status);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
}

bool cVNSIClient::processOSD_Connect(cRequestPacket &req) /* OPCODE 160 */
{
  m_Osd = new cVnsiOsdProvider(&m_socket);
  int osdWidth, osdHeight;
  double aspect;
  cDevice::PrimaryDevice()->GetOsdSize(osdWidth, osdHeight, aspect);

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(osdWidth);
  resp.add_U32(osdHeight);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processOSD_Disconnect() /* OPCODE 161 */
{
  if (m_Osd)
  {
    delete m_Osd;
    m_Osd = NULL;
  }
  return true;
}

bool cVNSIClient::processOSD_Hitkey(cRequestPacket &req) /* OPCODE 162 */
{
  if (m_Osd)
  {
    unsigned int key = req.extract_U32();
    cVnsiOsdProvider::SendKey(key);
  }
  return true;
}

/** OPCODE 180 - 189: VNSI network functions for deleted recording access */

bool cVNSIClient::processRECORDINGS_DELETED_Supported(cRequestPacket &req) /* OPCODE 180 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(VNSI_RET_OK);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_DELETED_GetCount(cRequestPacket &req) /* OPCODE 181 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());
#if VDRVERSNUM >= 20301
  LOCK_DELETEDRECORDINGS_READ;
  resp.add_U32(DeletedRecordings->Count());
#else
  resp.add_U32(DeletedRecordings.Count());
#endif
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_DELETED_GetList(cRequestPacket &req) /* OPCODE 182 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

  cMutexLock lock(&m_timerLock);

#if VDRVERSNUM >= 20301
  LOCK_DELETEDRECORDINGS_READ;
  for (const cRecording *recording = DeletedRecordings->First(); recording; recording = DeletedRecordings->Next(recording))
#else
  cThreadLock RecordingsLock(&Recordings);
  for (cRecording *recording = DeletedRecordings.First(); recording; recording = DeletedRecordings.Next(recording))
#endif
  {
#if APIVERSNUM >= 10705
    const cEvent *event = recording->Info()->GetEvent();
#else
    const cEvent *event = NULL;
#endif

    time_t recordingStart    = 0;
    int    recordingDuration = 0;
    if (event)
    {
      recordingStart    = event->StartTime();
      recordingDuration = event->Duration();
    }
    else
    {
      cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
      if (rc)
      {
        recordingStart    = rc->Timer()->StartTime();
        recordingDuration = rc->Timer()->StopTime() - recordingStart;
      }
      else
      {
#if APIVERSNUM >= 10727
        recordingStart = recording->Start();
#else
        recordingStart = recording->start;
#endif
      }
    }
    DEBUGLOG("GRI: RC: recordingStart=%lu recordingDuration=%i", recordingStart, recordingDuration);

    // recording_time
    resp.add_U32(recordingStart);

    // duration
    resp.add_U32(recordingDuration);

    // priority
#if APIVERSNUM >= 10727
    resp.add_U32(recording->Priority());
#else
    resp.add_U32(recording->priority);
#endif

    // lifetime
#if APIVERSNUM >= 10727
    resp.add_U32(recording->Lifetime());
#else
    resp.add_U32(recording->lifetime);
#endif

    // channel_name
    resp.add_String(recording->Info()->ChannelName() ? m_toUTF8.Convert(recording->Info()->ChannelName()) : "");

    char* fullname = strdup(recording->Name());
    char* recname = strrchr(fullname, FOLDERDELIMCHAR);
    char* directory = NULL;

    if(recname == NULL) {
      recname = fullname;
    }
    else {
      *recname = 0;
      recname++;
      directory = fullname;
    }

    // title
    resp.add_String(m_toUTF8.Convert(recname));

    // subtitle
    if (!isempty(recording->Info()->ShortText()))
      resp.add_String(m_toUTF8.Convert(recording->Info()->ShortText()));
    else
      resp.add_String("");

    // description
    if (!isempty(recording->Info()->Description()))
      resp.add_String(m_toUTF8.Convert(recording->Info()->Description()));
    else
      resp.add_String("");

    // directory
    if(directory != NULL) {
      char* p = directory;
      while(*p != 0) {
        if(*p == FOLDERDELIMCHAR) *p = '/';
        if(*p == '_') *p = ' ';
        p++;
      }
      while(*directory == '/') directory++;
    }

    resp.add_String((isempty(directory)) ? "" : m_toUTF8.Convert(directory));

    // filename / uid of recording
    uint32_t uid = cRecordingsCache::GetInstance().Register(recording, false);
    resp.add_U32(uid);

    free(fullname);
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_DELETED_Delete(cRequestPacket &req) /* OPCODE 183 */
{
  cResponsePacket resp;
  resp.init(req.getRequestID());

  cString recName;
  cRecording* recording = NULL;

#if VDRVERSNUM >= 20102
  cLockFile LockFile(cVideoDirectory::Name());
#else
  cLockFile LockFile(VideoDirectory);
#endif
  if (LockFile.Lock())
  {
    uint32_t uid = req.extract_U32();

#if VDRVERSNUM >= 20301
    LOCK_DELETEDRECORDINGS_WRITE;
    for (recording = DeletedRecordings->First(); recording; recording = DeletedRecordings->Next(recording))
#else
    cThreadLock DeletedRecordingsLock(&DeletedRecordings);
    for (recording = DeletedRecordings.First(); recording; recording = DeletedRecordings.Next(recording))
#endif
    {
      if (uid == CreateStringHash(recording->FileName()))
      {
#if VDRVERSNUM >= 20102
        if (!cVideoDirectory::RemoveVideoFile(recording->FileName()))
#else
        if (!RemoveVideoFile(recording->FileName()))
#endif
        {
          ERRORLOG("Error while remove deleted recording (%s)", recording->FileName());
          resp.add_U32(VNSI_RET_ERROR);
        }
        else
        {
#if VDRVERSNUM >= 20301
          DeletedRecordings->Del(recording);
          DeletedRecordings->Update();
#else
          DeletedRecordings.Del(recording);
          DeletedRecordings.Update();
#endif
          INFOLOG("Recording \"%s\" permanent deleted", recording->FileName());
          resp.add_U32(VNSI_RET_OK);
        }
        break;
      }
    }
  }

  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

bool cVNSIClient::Undelete(cRecording* recording)
{
  DEBUGLOG("undelete recording: %s", recording->Name());

  char *NewName = strdup(recording->FileName());
  char *ext = strrchr(NewName, '.');
  if (ext && strcmp(ext, ".del") == 0)
  {
    strncpy(ext, ".rec", strlen(ext));
    if (!access(NewName, F_OK))
    {
      ERRORLOG("Recording with the same name exists (%s)", NewName);
      OsdStatusMessage(*cString::sprintf("%s (%s)", tr("Recording with the same name exists"), NewName));
    }
    else
    {
      if (access(recording->FileName(), F_OK) == 0)
      {
#if VDRVERSNUM >= 20102
        if (!cVideoDirectory::RenameVideoFile(recording->FileName(), NewName))
#else
        if (!RenameVideoFile(recording->FileName(), NewName))
#endif
        {
          ERRORLOG("Error while rename deleted recording (%s) to (%s)", recording->FileName(), NewName);
        }

        cIndexFile *index = new cIndexFile(NewName, false, recording->IsPesRecording());
        int LastFrame = index->Last() - 1;
        if (LastFrame > 0)
        {
          uint16_t FileNumber = 0;
          off_t FileOffset = 0;
          index->Get(LastFrame, &FileNumber, &FileOffset);
          delete index;
          if (FileNumber == 0)
          {
            ERRORLOG("while read last filenumber (%s)", NewName);
            OsdStatusMessage(*cString::sprintf("%s (%s)", tr("Error while read last filenumber"), NewName));
          }
          else
          {
            for (int i = 1; i <= FileNumber; i++)
            {
              cString temp = cString::sprintf(recording->IsPesRecording() ? "%s/%03d.vdr" : "%s/%05d.ts", (const char *)NewName, i);
              if (access(*temp, R_OK) != 0)
              {
                i = FileNumber;
                OsdStatusMessage(*cString::sprintf("%s %03d (%s)", tr("Error while accessing vdrfile"), i, NewName));
              }
            }
          }
        }
        else
        {
          delete index;
          ERRORLOG("accessing indexfile (%s)", NewName);
          OsdStatusMessage(*cString::sprintf("%s (%s)", tr("Error while accessing indexfile"), NewName));
        }

#if VDRVERSNUM >= 20301
        LOCK_RECORDINGS_WRITE;
        LOCK_DELETEDRECORDINGS_WRITE;
        DeletedRecordings->Del(recording);
        Recordings->Update();
        DeletedRecordings->Update();
#else
        DeletedRecordings.Del(recording);
        Recordings.Update();
        DeletedRecordings.Update();
#endif
      }
      else
      {
        ERRORLOG("deleted recording '%s' vanished", recording->FileName());
        OsdStatusMessage(*cString::sprintf("%s \"%s\"", tr("Deleted recording vanished"), recording->FileName()));
      }
    }
  }
  free(NewName);
  return true;
}

bool cVNSIClient::processRECORDINGS_DELETED_Undelete(cRequestPacket &req) /* OPCODE 184 */
{
  int ret = VNSI_RET_DATAUNKNOWN;

#if VDRVERSNUM >= 20102
  cLockFile LockFile(cVideoDirectory::Name());
#else
  cLockFile LockFile(VideoDirectory);
#endif
  if (LockFile.Lock())
  {
    uint32_t uid = req.extract_U32();

#if VDRVERSNUM >= 20301
    LOCK_DELETEDRECORDINGS_WRITE;
    for (cRecording* recording = DeletedRecordings->First(); recording; recording = DeletedRecordings->Next(recording))
#else
    cThreadLock DeletedRecordingsLock(&DeletedRecordings);
    for (cRecording* recording = DeletedRecordings.First(); recording; recording = DeletedRecordings.Next(recording))
#endif
    {
      if (uid == CreateStringHash(recording->FileName()))
      {
        if (Undelete(recording))
        {
          INFOLOG("Recording \"%s\" undeleted", recording->FileName());
          ret = VNSI_RET_OK;
        }
        else
          ret = VNSI_RET_ERROR;
        break;
      }
    }
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(ret);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());
  return true;
}

bool cVNSIClient::processRECORDINGS_DELETED_DeleteAll(cRequestPacket &req) /* OPCODE 185 */
{
  int ret = VNSI_RET_OK;

#if VDRVERSNUM >= 20102
  cLockFile LockFile(cVideoDirectory::Name());
#else
  cLockFile LockFile(VideoDirectory);
#endif

  if (LockFile.Lock())
  {
#if VDRVERSNUM >= 20301
    LOCK_DELETEDRECORDINGS_WRITE;
    for (cRecording *recording = DeletedRecordings->First(); recording; )
#else
    cThreadLock DeletedRecordingsLock(&DeletedRecordings);
    for (cRecording *recording = DeletedRecordings.First(); recording; )
#endif
    {
#if VDRVERSNUM >= 20301
      cRecording *next = DeletedRecordings->Next(recording);
#else
      cRecording *next = DeletedRecordings.Next(recording);
#endif
#if VDRVERSNUM >= 20102
      if (!cVideoDirectory::RemoveVideoFile(recording->FileName()))
#else
      if (!RemoveVideoFile(recording->FileName()))
#endif
      {
        ERRORLOG("Error while remove deleted recording (%s)", recording->FileName());
        ret = VNSI_RET_ERROR;
        break;
      }
      else
        INFOLOG("Recording \"%s\" permanent deleted", recording->FileName());
      recording = next;
    }
#if VDRVERSNUM >= 20301
    DeletedRecordings->Clear();
    DeletedRecordings->Update();
#else
    DeletedRecordings.Clear();
    DeletedRecordings.Update();
#endif
  }

  cResponsePacket resp;
  resp.init(req.getRequestID());
  resp.add_U32(ret);
  resp.finalise();
  m_socket.write(resp.getPtr(), resp.getLen());

  return true;
}

// part of this method is taken from XVDR
cString cVNSIClient::CreatePiconRef(const cChannel* channel)
{
  int hash = 0;

  if(cSource::IsSat(channel->Source()))
  {
    int16_t pos = channel->Source() & cSource::st_Pos;
    hash = pos;

#if VDRVERSNUM >= 20101
    if(hash < 0)
    {
      hash = 3600 + hash;
    }
#endif

    hash = hash << 16;
  }
  else if(cSource::IsCable(channel->Source()))
    hash = 0xFFFF0000;
  else if(cSource::IsTerr(channel->Source()))
    hash = 0xEEEE0000;
  else if(cSource::IsAtsc(channel->Source()))
    hash = 0xDDDD0000;

  cString serviceref = cString::sprintf("1_0_%i_%X_%X_%X_%X_0_0_0",
                                cVNSIChannelFilter::IsRadio(channel) ? 2 : (channel->Vtype() == 27) ? 19 : 1,
                                channel->Sid(),
                                channel->Tid(),
                                channel->Nid(),
                                hash);

  return serviceref;
}
