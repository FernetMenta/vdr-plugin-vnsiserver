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

#ifndef __CHANNELSCAN_CONTROL__
#define __CHANNELSCAN_CONTROL__

#include <vector>
#include <vdr/plugin.h>
#include "wirbelscan_services.h"

#define RECEIVER_SYSTEM_DVB_T         0
#define RECEIVER_SYSTEM_DVB_C         1
#define RECEIVER_SYSTEM_DVB_S         2
#define RECEIVER_SYSTEM_ATSC          3
#define RECEIVER_SYSTEM_ANALOG_TV     4
#define RECEIVER_SYSTEM_ANALOG_RADIO  5

typedef enum scantype
{
  DVB_TERR    = 0,
  DVB_CABLE   = 1,
  DVB_SAT     = 2,
  PVRINPUT    = 3,
  PVRINPUT_FM = 4,
  DVB_ATSC    = 5,
} scantype_t;

struct sScanServiceData
{
  int    type;

  bool        scan_tv;
  bool        scan_radio;
  bool        scan_fta;
  bool        scan_scrambled;
  bool        scan_hd;

  int         CountryIndex;

  int         DVBC_Inversion;
  int         DVBC_Symbolrate;
  int         DVBC_QAM;

  int         DVBT_Inversion;

  int         SatIndex;

  int         ATSC_Type;
};

struct scannerEntry
{
  int index;
  const char *name;
  const char *longName;
};

typedef std::vector<scannerEntry> scannerEntryList;

class cVNSIClient;

class CScanControl : public cThread
{
public:
  CScanControl(cVNSIClient *client);
  ~CScanControl();

  bool IsSupported();
  bool StartScan(sScanServiceData &data);
  bool StopScan();
  bool GetCountries(scannerEntryList &list);
  bool GetSatellites(scannerEntryList &list);

  bool SupportsDVB_T() { return m_receiverSystems[RECEIVER_SYSTEM_DVB_T]; }
  bool SupportsDVB_C() { return m_receiverSystems[RECEIVER_SYSTEM_DVB_C]; }
  bool SupportsDVB_S() { return m_receiverSystems[RECEIVER_SYSTEM_DVB_S]; }
  bool SupportsAnalogTV() { return m_receiverSystems[RECEIVER_SYSTEM_ANALOG_TV]; }
  bool SupportsAnalogRadio() { return m_receiverSystems[RECEIVER_SYSTEM_ANALOG_RADIO]; }
  bool SupportsATSC() { return m_receiverSystems[RECEIVER_SYSTEM_ATSC]; }

  virtual void Action();

private:
  bool LoadScanner();
  void PutCommand(WIRBELSCAN_SERVICE::s_cmd command);

  cVNSIClient       *m_client;
  bool               m_isChecked;
  bool               m_isNotSupported;
  bool               m_isNotTpSupported;
  bool               m_receiverSystems[6];
  cPlugin           *m_channelScanPlugin;
  bool               m_singleScan;
  bool               m_finishReported;
  WIRBELSCAN_SERVICE::SListItem         *m_cbuf;
  WIRBELSCAN_SERVICE::SListItem         *m_sbuf;
  WIRBELSCAN_SERVICE::cPreAllocBuffer    m_countryBuffer;
  WIRBELSCAN_SERVICE::cPreAllocBuffer    m_satBuffer;
  WIRBELSCAN_SERVICE::cWirbelscanInfo   *m_scanInformation;
  WIRBELSCAN_SERVICE::cWirbelscanStatus  m_scanStatus;
  WIRBELSCAN_SERVICE::cWirbelscanScanSetup m_setup;
  int                m_lastChannelCount;
};

#endif // __CHANNELSCAN_CONTROL__
