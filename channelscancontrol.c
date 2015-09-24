/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2010,2015 Alwin Esch (Team KODI)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
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

#include <vdr/menu.h>
#include <vdr/status.h>

#include "channelscancontrol.h"
#include "vnsiclient.h"

using namespace WIRBELSCAN_SERVICE;

/*!
 * versions definitions
 */
#define MIN_CMDVERSION     1    // command api 0001
#define MIN_STATUSVERSION  1    // query status
#define MIN_SETUPVERSION   1    // get/put setup, GetSetup#XXXX/SetSetup#XXXX
#define MIN_COUNTRYVERSION 1    // get list of country IDs and Names
#define MIN_SATVERSION     1    // get list of sat IDs and Names
#define MIN_USERVERSION    1    // scan user defined transponder

#define SCAN_TV        ( 1 << 0 )
#define SCAN_RADIO     ( 1 << 1 )
#define SCAN_FTA       ( 1 << 2 )
#define SCAN_SCRAMBLED ( 1 << 3 )
#define SCAN_HD        ( 1 << 4 )

/*!
 * macro definitions
 */
#define SETSCAN  0
#define SCANNING 1
#define SCANDONE 2
#define CHECKVERSION(a,b,c) p=strchr((char *) m_scanInformation->a,'#') + 1; sscanf(p,"%d ",&version); if (version < b) c = true;
#define CHECKLIMITS(a,v,_min,_max,_def) a=v; if ((a<_min) || (a>_max)) a=_def;
#define freeAndNull(p)   if(p) { free(p);   p=NULL; }
#define deleteAndNull(p) if(p) { delete(p); p=NULL; }

CScanControl::CScanControl(cVNSIClient *client)
  : m_client(client),
    m_isChecked(false),
    m_isNotSupported(true),
    m_channelScanPlugin(NULL),
    m_singleScan(0),
    m_cbuf(NULL),
    m_sbuf(NULL),
    m_scanInformation(NULL)
{
}

CScanControl::~CScanControl()
{
  if (m_scanInformation)
    delete m_scanInformation;
  freeAndNull(m_cbuf);
  freeAndNull(m_sbuf);
}

bool CScanControl::IsSupported()
{
  if (!m_isChecked)
    LoadScanner();

  return !m_isNotSupported;
}

bool CScanControl::LoadScanner()
{
  /*!
   * Plugin present?
   */
  m_channelScanPlugin = cPluginManager::GetPlugin("wirbelscan");
  if (m_channelScanPlugin == NULL)
    return false;

  /*!
   * This plugin version compatibel to wirbelscan?
   */
  char *p;
  m_scanInformation = new cWirbelscanInfo;
  if (asprintf(&p, "%s%s", SPlugin, SInfo) < 0 ||
      !m_channelScanPlugin->Service("wirbelscan_GetVersion", m_scanInformation))
  {
    m_client->OsdStatusMessage(*cString::sprintf("%s", tr("Your scanner version doesnt support services - Please upgrade.")));
    free(p);
    return false;
  }
  free(p);

  int version = 0;
  m_isNotSupported = false;
  CHECKVERSION(CommandVersion,MIN_CMDVERSION,     m_isNotSupported);
  CHECKVERSION(StatusVersion, MIN_STATUSVERSION,  m_isNotSupported);
  CHECKVERSION(SetupVersion,  MIN_SETUPVERSION,   m_isNotSupported);
  CHECKVERSION(CountryVersion,MIN_COUNTRYVERSION, m_isNotSupported);
  CHECKVERSION(SatVersion,    MIN_SATVERSION,     m_isNotSupported);
  CHECKVERSION(UserVersion,   MIN_USERVERSION,    m_isNotSupported);
  if (m_isNotSupported)
  {
    m_client->OsdStatusMessage(*cString::sprintf("%s", tr("Your scanner version is to old - Please upgrade." )));
    return false;
  }

  /*!
   * Check which receivers are present
   */
  int cardnr = 0;
  cDevice *device;
  memset(&m_receiverSystems, 0, sizeof(m_receiverSystems));
  while ((device = cDevice::GetDevice(cardnr++)))
  {
    if (device->ProvidesSource(cSource::stTerr))
      m_receiverSystems[RECEIVER_SYSTEM_DVB_T] = true;
    if (device->ProvidesSource(cSource::stCable))
      m_receiverSystems[RECEIVER_SYSTEM_DVB_C] = true;
    if (device->ProvidesSource(cSource::stSat))
      m_receiverSystems[RECEIVER_SYSTEM_DVB_S] = true;
    if (device->ProvidesSource(cSource::stAtsc))
      m_receiverSystems[RECEIVER_SYSTEM_ATSC] = true;
  }

  if (cPluginManager::GetPlugin("pvrinput"))
  {
    m_receiverSystems[RECEIVER_SYSTEM_ANALOG_TV]    = true;
    m_receiverSystems[RECEIVER_SYSTEM_ANALOG_RADIO] = true;
  }

  m_countryBuffer.size = 0;
  m_countryBuffer.count = 0;
  m_countryBuffer.buffer = NULL;
  if (asprintf(&p, "%sGet%s", SPlugin, SCountry) < 0)
    return false;
  m_channelScanPlugin->Service(p, &m_countryBuffer);                      // query buffer size.
  m_cbuf = (SListItem*) malloc(m_countryBuffer.size * sizeof(SListItem)); // now, allocate memory.
  m_countryBuffer.buffer = m_cbuf;                                        // assign buffer
  m_channelScanPlugin->Service(p, &m_countryBuffer);                      // fill buffer with values.
  free(p);

  m_satBuffer.size = 0;
  m_satBuffer.count = 0;
  m_satBuffer.buffer = NULL;
  if (asprintf(&p, "%sGet%s", SPlugin, SSat) < 0)
    return false;
  m_channelScanPlugin->Service(p, &m_satBuffer);                          // query buffer size.
  m_sbuf = (SListItem*) malloc(m_satBuffer.size * sizeof(SListItem));     // now, allocate memory.
  m_satBuffer.buffer = m_sbuf;                                            // assign buffer
  m_channelScanPlugin->Service(p, &m_satBuffer);                          // fill buffer with values.
  free(p);

  if (asprintf(&p, "%sGet%s", SPlugin, SSetup) < 0)
    return false;
  m_channelScanPlugin->Service(p, &m_setup);
  free(p);

  return true;
}

bool CScanControl::StartScan(sScanServiceData &data)
{
  if (m_isNotSupported)
    return false;

  m_finishReported = false;

  CHECKLIMITS(m_setup.SatId           , data.SatIndex        , 0 , 0xFFFF, 0);
  CHECKLIMITS(m_setup.CountryId       , data.CountryIndex    , 0 , 0xFFFF, 0);
  CHECKLIMITS(m_setup.DVB_Type        , data.type            , 0 , 5     , 0);
  CHECKLIMITS(m_setup.DVBT_Inversion  , data.DVBT_Inversion  , 0 , 1     , 0);
  CHECKLIMITS(m_setup.DVBC_Inversion  , data.DVBC_Inversion  , 0 , 1     , 0);
  CHECKLIMITS(m_setup.DVBC_Symbolrate , data.DVBC_Symbolrate , 0 , 16    , 0);
  CHECKLIMITS(m_setup.DVBC_QAM        , data.DVBC_QAM        , 0 , 4     , 0);
  CHECKLIMITS(m_setup.ATSC_type       , data.ATSC_Type       , 0 , 1     , 0);

  m_setup.scanflags  = data.scan_tv        ? SCAN_TV        : 0;
  m_setup.scanflags |= data.scan_radio     ? SCAN_RADIO     : 0;
  m_setup.scanflags |= data.scan_scrambled ? SCAN_SCRAMBLED : 0;
  m_setup.scanflags |= data.scan_fta       ? SCAN_FTA       : 0;
  m_setup.scanflags |= data.scan_hd        ? SCAN_HD        : 0;

#if VDRVERSNUM >= 20301
  {
    LOCK_CHANNELS_READ;
    m_lastChannelCount = Channels->Count();
  }
#else
  m_lastChannelCount = Channels.Count();
#endif

  char *s;
  if (asprintf(&s, "%sSet%s", SPlugin, SSetup) < 0)
    return false;
  m_channelScanPlugin->Service(s, &m_setup);
  free(s);

  PutCommand(CmdStartScan);
  Start();// start polling thread
  return true;
}

bool CScanControl::StopScan()
{
  if (m_isNotSupported)
    return false;

  PutCommand(CmdStopScan);
  cCondWait::SleepMs(500);
  Cancel(2);
  if (!m_finishReported)
  {
    if (m_scanStatus.status != StatusStopped)
    {
      m_scanStatus.status = StatusStopped;
      m_client->processSCAN_SetStatus(m_scanStatus.status);
      INFOLOG("Stopping scan plugin not reported back and stop maybe not complete confirmed");
    }
    m_client->processSCAN_IsFinished();
  }

  return true;
}

bool CScanControl::GetCountries(scannerEntryList &list)
{
  for (uint32_t i = 0; i < m_countryBuffer.count; i++)
  {
    scannerEntry entry;
    entry.index    = m_countryBuffer.buffer[i].id;
    entry.name     = m_countryBuffer.buffer[i].short_name;
    entry.longName = m_countryBuffer.buffer[i].full_name;
    list.push_back(entry);
  }

  return !list.empty();
}

bool CScanControl::GetSatellites(scannerEntryList &list)
{
  for (uint32_t i = 0; i < m_satBuffer.count; i++)
  {
    scannerEntry entry;
    entry.index    = m_satBuffer.buffer[i].id;
    entry.name     = m_satBuffer.buffer[i].short_name;
    entry.longName = m_satBuffer.buffer[i].full_name;
    list.push_back(entry);
  }

  return !list.empty();
}

void CScanControl::PutCommand(WIRBELSCAN_SERVICE::s_cmd command)
{
  cWirbelscanCmd cmd;
  char * request;
  if (asprintf(&request, "%s%s", SPlugin, SCommand) < 0)
    return;

  cmd.cmd = command;
  m_channelScanPlugin->Service(request, &cmd);
  free(request);
}

void CScanControl::Action(void)
{
  while (Running())
  {
    cCondWait::SleepMs(1000);

    char * s;
    if (asprintf(&s, "%sGet%s", SPlugin, SStatus) < 0)
      return;
    m_channelScanPlugin->Service(s, &m_scanStatus);
    free(s);

    m_client->processSCAN_SetStatus(m_scanStatus.status);
    m_client->processSCAN_SetPercentage(m_scanStatus.progress);
    m_client->processSCAN_SetSignalStrength(m_scanStatus.strength, false);
    m_client->processSCAN_SetDeviceInfo(m_scanStatus.curr_device);
    m_client->processSCAN_SetTransponder(m_scanStatus.transponder);


    int noOfChannels;
#if VDRVERSNUM >= 20301
    {
      LOCK_CHANNELS_READ;
      noOfChannels = Channels->Count();
    }
#else
    noOfChannels = Channels.Count();
#endif

    for (int i = 0; i < noOfChannels-m_lastChannelCount; i++)
    {
#if VDRVERSNUM >= 20301
      LOCK_CHANNELS_READ;
      const cChannel *channel = Channels->GetByNumber(Channels->Count()-i);
#else
      cChannel *channel = Channels.GetByNumber(Channels.Count()-i);
#endif
      m_client->processSCAN_NewChannel(channel->Name(), channel->Vpid() == 0, channel->Ca() > 0, channel->Vtype() > 2);
    }

#if VDRVERSNUM >= 20301
  {
    LOCK_CHANNELS_READ;
    m_lastChannelCount = Channels->Count();
  }
#else
  m_lastChannelCount = Channels.Count();
#endif

    if (m_scanStatus.status == StatusStopped)
    {
      m_client->processSCAN_IsFinished();
      m_finishReported = true;
      break;
    }
  }
  Cancel(0);
}
