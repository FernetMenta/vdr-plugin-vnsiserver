/*
 *      vdr-plugin-vnsi - XBMC server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
 *
 *      http://www.xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifndef VNSI_CLIENT_H
#define VNSI_CLIENT_H

#include <vdr/thread.h>
#include <vdr/tools.h>
#include <vdr/receiver.h>
#include <vdr/status.h>

#include "config.h"
#include "cxsocket.h"
#include "channelscancontrol.h"

#include <map>
#include <string>

class cChannel;
class cDevice;
class cLiveStreamer;
class cRequestPacket;
class cResponsePacket;
class cRecPlayer;
class cCmdControl;
class cVnsiOsdProvider;

class cVNSIClient : public cThread
                  , public cStatus
{
private:

  unsigned int     m_Id;
  cxSocket         m_socket;
  bool             m_loggedIn;
  bool             m_StatusInterfaceEnabled;
  cLiveStreamer   *m_Streamer;
  bool             m_isStreaming;
  bool             m_bSupportRDS;
  cString          m_ClientAddress;
  cRecPlayer      *m_RecPlayer;
  cRequestPacket  *m_req;
  cResponsePacket *m_resp;
  cCharSetConv     m_toUTF8;
  uint32_t         m_protocolVersion;
  cMutex           m_msgLock;
  static cMutex    m_timerLock;
  cVnsiOsdProvider *m_Osd;
  CScanControl      m_ChannelScanControl;
  static bool       m_inhibidDataUpdates;
  typedef struct
  {
    int attempts;
    time_t lastEvent;
  } sEpgUpdate;
  std::map<int, sEpgUpdate> m_epgUpdate;

protected:

  bool processRequest(cRequestPacket* req);

  virtual void Action(void);

  virtual void TimerChange(const cTimer *Timer, eTimerChange Change);
  virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
  virtual void OsdStatusMessage(const char *Message);
  virtual void ChannelChange(const cChannel *Channel);

public:

  cVNSIClient(int fd, unsigned int id, const char *ClientAdr);
  virtual ~cVNSIClient();

  void ChannelsChange();
  void RecordingsChange();
  void TimerChange();
  void EpgChange();
  static bool InhibidDataUpdates() { return m_inhibidDataUpdates; }

  unsigned int GetID() { return m_Id; }

protected:

  void SetLoggedIn(bool yesNo) { m_loggedIn = yesNo; }
  void SetStatusInterface(bool yesNo) { m_StatusInterfaceEnabled = yesNo; }
  bool StartChannelStreaming(const cChannel *channel, int32_t priority, uint8_t timeshift, uint32_t timeout);
  void StopChannelStreaming();

private:

  typedef struct {
    bool automatic;
    bool radio;
    std::string name;
  } ChannelGroup;

  std::map<std::string, ChannelGroup> m_channelgroups[2];

  bool process_Login();
  bool process_GetTime();
  bool process_EnableStatusInterface();
  bool process_Ping();
  bool process_GetSetup();
  bool process_StoreSetup();

  bool processChannelStream_Open();
  bool processChannelStream_Close();
  bool processChannelStream_Seek();

  bool processRecStream_Open();
  bool processRecStream_Close();
  bool processRecStream_GetBlock();
  bool processRecStream_PositionFromFrameNumber();
  bool processRecStream_FrameNumberFromPosition();
  bool processRecStream_GetIFrame();
  bool processRecStream_GetLength();

  bool processCHANNELS_GroupsCount();
  bool processCHANNELS_ChannelsCount();
  bool processCHANNELS_GroupList();
  bool processCHANNELS_GetChannels();
  bool processCHANNELS_GetGroupMembers();
  bool processCHANNELS_GetCaids();
  bool processCHANNELS_GetWhitelist();
  bool processCHANNELS_GetBlacklist();
  bool processCHANNELS_SetWhitelist();
  bool processCHANNELS_SetBlacklist();

  void CreateChannelGroups(bool automatic);

  bool processTIMER_GetCount();
  bool processTIMER_Get();
  bool processTIMER_GetList();
  bool processTIMER_Add();
  bool processTIMER_Delete();
  bool processTIMER_Update();

  bool processRECORDINGS_GetDiskSpace();
  bool processRECORDINGS_GetCount();
  bool processRECORDINGS_GetList();
  bool processRECORDINGS_GetInfo();
  bool processRECORDINGS_Rename();
  bool processRECORDINGS_Delete();
  bool processRECORDINGS_Move();
  bool processRECORDINGS_GetEdl();
  bool processRECORDINGS_DELETED_Supported();
  bool processRECORDINGS_DELETED_GetCount();
  bool processRECORDINGS_DELETED_GetList();
  bool processRECORDINGS_DELETED_Delete();
  bool processRECORDINGS_DELETED_Undelete();
  bool processRECORDINGS_DELETED_DeleteAll();

  bool processEPG_GetForChannel();

  bool processSCAN_ScanSupported();
  bool processSCAN_GetSupportedTypes();
  bool processSCAN_GetCountries();
  bool processSCAN_GetSatellites();
  bool processSCAN_Start();
  bool processSCAN_Stop();

  bool Undelete(cRecording* recording);

  bool processOSD_Connect();
  bool processOSD_Disconnect();
  bool processOSD_Hitkey();

  cString CreatePiconRef(cChannel* channel);

private:
  /** Static callback functions to interact with wirbelscan plugin over
      the plugin service interface */
  friend class CScanControl;

  void processSCAN_SetPercentage(int percent);
  void processSCAN_SetSignalStrength(int strength, bool locked);
  void processSCAN_SetDeviceInfo(const char *Info);
  void processSCAN_SetTransponder(const char *Info);
  void processSCAN_NewChannel(const char *Name, bool isRadio, bool isEncrypted, bool isHD);
  void processSCAN_IsFinished();
  void processSCAN_SetStatus(int status);
};

#endif // VNSI_CLIENT_H
