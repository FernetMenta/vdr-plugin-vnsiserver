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
class CVNSITimers;

class cVNSIClient : public cThread
                  , public cStatus
{
  unsigned int     m_Id;
  cxSocket         m_socket;
  bool             m_loggedIn = false;
  bool             m_StatusInterfaceEnabled = false;
  cLiveStreamer   *m_Streamer = nullptr;
  bool             m_isStreaming = false;
  bool             m_bSupportRDS = false;
  cString          m_ClientAddress;
  cRecPlayer      *m_RecPlayer = nullptr;
  cCharSetConv     m_toUTF8;
  uint32_t         m_protocolVersion;
  cMutex           m_msgLock;
  static cMutex    m_timerLock;
  cVnsiOsdProvider *m_Osd = nullptr;
  CScanControl      m_ChannelScanControl;
  static bool       m_inhibidDataUpdates;
  typedef struct
  {
    int attempts;
    time_t lastEvent;
  } sEpgUpdate;
  std::map<int, sEpgUpdate> m_epgUpdate;
  CVNSITimers &m_vnsiTimers;

protected:

  bool processRequest(cRequestPacket &req);

  virtual void Action(void) override;
  virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) override;
  virtual void OsdStatusMessage(const char *Message) override;
#if VDRVERSNUM >= 20104
  virtual void ChannelChange(const cChannel *Channel) override;
#endif

public:

  cVNSIClient(int fd, unsigned int id, const char *ClientAdr, CVNSITimers &timers);
  virtual ~cVNSIClient();

  cVNSIClient(const cVNSIClient &) = delete;
  cVNSIClient &operator=(const cVNSIClient &) = delete;

  void ChannelsChange();
  void RecordingsChange();
  void SignalTimerChange();
  bool EpgChange();
  static bool InhibidDataUpdates() { return m_inhibidDataUpdates; }

  unsigned int GetID() { return m_Id; }

protected:

  void SetLoggedIn(bool yesNo) { m_loggedIn = yesNo; }
  void SetStatusInterface(bool yesNo) { m_StatusInterfaceEnabled = yesNo; }
  bool StartChannelStreaming(cResponsePacket &resp, const cChannel *channel, int32_t priority, uint8_t timeshift, uint32_t timeout);
  void StopChannelStreaming();

private:

  typedef struct {
    bool automatic;
    bool radio;
    std::string name;
  } ChannelGroup;

  std::map<std::string, ChannelGroup> m_channelgroups[2];

  bool process_Login(cRequestPacket &r);
  bool process_GetTime(cRequestPacket &r);
  bool process_EnableStatusInterface(cRequestPacket &r);
  bool process_Ping(cRequestPacket &r);
  bool process_GetSetup(cRequestPacket &r);
  bool process_StoreSetup(cRequestPacket &r);

  bool processChannelStream_Open(cRequestPacket &r);
  bool processChannelStream_Close(cRequestPacket &req);
  bool processChannelStream_Seek(cRequestPacket &r);

  bool processRecStream_Open(cRequestPacket &r);
  bool processRecStream_Close(cRequestPacket &r);
  bool processRecStream_GetBlock(cRequestPacket &r);
  bool processRecStream_PositionFromFrameNumber(cRequestPacket &r);
  bool processRecStream_FrameNumberFromPosition(cRequestPacket &r);
  bool processRecStream_GetIFrame(cRequestPacket &r);
  bool processRecStream_GetLength(cRequestPacket &r);

  bool processCHANNELS_GroupsCount(cRequestPacket &r);
  bool processCHANNELS_ChannelsCount(cRequestPacket &r);
  bool processCHANNELS_GroupList(cRequestPacket &r);
  bool processCHANNELS_GetChannels(cRequestPacket &r);
  bool processCHANNELS_GetGroupMembers(cRequestPacket &r);
  bool processCHANNELS_GetCaids(cRequestPacket &r);
  bool processCHANNELS_GetWhitelist(cRequestPacket &r);
  bool processCHANNELS_GetBlacklist(cRequestPacket &r);
  bool processCHANNELS_SetWhitelist(cRequestPacket &r);
  bool processCHANNELS_SetBlacklist(cRequestPacket &r);

  void CreateChannelGroups(bool automatic);

  bool processTIMER_GetCount(cRequestPacket &r);
  bool processTIMER_Get(cRequestPacket &r);
  bool processTIMER_GetList(cRequestPacket &r);
  bool processTIMER_Add(cRequestPacket &r);
  bool processTIMER_Delete(cRequestPacket &r);
  bool processTIMER_Update(cRequestPacket &r);
  bool processTIMER_GetTypes(cRequestPacket &r);

  bool processRECORDINGS_GetDiskSpace(cRequestPacket &r);
  bool processRECORDINGS_GetCount(cRequestPacket &r);
  bool processRECORDINGS_GetList(cRequestPacket &r);
  bool processRECORDINGS_GetInfo(cRequestPacket &r);
  bool processRECORDINGS_Rename(cRequestPacket &r);
  bool processRECORDINGS_Delete(cRequestPacket &r);
  bool processRECORDINGS_Move(cRequestPacket &r);
  bool processRECORDINGS_GetEdl(cRequestPacket &r);
  bool processRECORDINGS_DELETED_Supported(cRequestPacket &r);
  bool processRECORDINGS_DELETED_GetCount(cRequestPacket &r);
  bool processRECORDINGS_DELETED_GetList(cRequestPacket &r);
  bool processRECORDINGS_DELETED_Delete(cRequestPacket &r);
  bool processRECORDINGS_DELETED_Undelete(cRequestPacket &r);
  bool processRECORDINGS_DELETED_DeleteAll(cRequestPacket &r);

  bool processEPG_GetForChannel(cRequestPacket &r);

  bool processSCAN_ScanSupported(cRequestPacket &r);
  bool processSCAN_GetSupportedTypes(cRequestPacket &r);
  bool processSCAN_GetCountries(cRequestPacket &r);
  bool processSCAN_GetSatellites(cRequestPacket &r);
  bool processSCAN_Start(cRequestPacket &r);
  bool processSCAN_Stop(cRequestPacket &r);

  bool Undelete(cRecording* recording);

  bool processOSD_Connect(cRequestPacket &req);
  bool processOSD_Disconnect();
  bool processOSD_Hitkey(cRequestPacket &req);

  cString CreatePiconRef(const cChannel* channel);

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
