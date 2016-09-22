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

#include "vnsi.h"
#include "vnsicommand.h"
#include "setup.h"

#include <getopt.h>
#include <vdr/plugin.h>

cPluginVNSIServer* cPluginVNSIServer::VNSIServer = NULL;

cPluginVNSIServer::cPluginVNSIServer(void)
{
  Server = NULL;
  VNSIServer = NULL;
}

const char *cPluginVNSIServer::CommandLineHelp(void)
{
    return "  -t n, --timeout=n      stream data timeout in seconds (default: 10)\n"
           "  -d  , --device         act as the primary device\n"
           "  -s n, --test=n         TS stream test file to simulate as channel\n"
           "  -p n, --port=n         tcp port to listen on\n";
}

bool cPluginVNSIServer::ProcessArgs(int argc, char *argv[])
{
  // Implement command line argument processing here if applicable.
  static struct option long_options[] = {
       { "port",     required_argument, NULL, 'p' },
       { "timeout",  required_argument, NULL, 't' },
       { "device",   no_argument,       NULL, 'd' },
       { "test",     required_argument, NULL, 'T' },
       { NULL,       no_argument,       NULL,  0  }
     };

  int c;

  while ((c = getopt_long(argc, argv, "t:dT:p:", long_options, NULL)) != -1) {
        switch (c) {
          case 'p': if(optarg != NULL) VNSIServerConfig.listen_port = atoi(optarg);
                    break;
          case 't': if(optarg != NULL) VNSIServerConfig.stream_timeout = atoi(optarg);
                    break;
          case 'd': VNSIServerConfig.device = true;
                    break;
          case 'T': if(optarg != NULL) {
                    VNSIServerConfig.testStreamFile = optarg;

                    struct stat file_stat;
                    if (stat(VNSIServerConfig.testStreamFile, &file_stat) == 0) {
                      VNSIServerConfig.testStreamActive = true;
                      printf("vnsiserver: requested test stream file '%s' played now on all channels\n", *VNSIServerConfig.testStreamFile);
                      }
                    else
                      printf("vnsiserver: requested test stream file '%s' not present, started without\n", *VNSIServerConfig.testStreamFile);
                      }
                    break;
          default:  return false;
          }
        }
  return true;
}

bool cPluginVNSIServer::Initialize(void)
{
  // Initialize any background activities the plugin shall perform.
  VNSIServerConfig.ConfigDirectory = ConfigDirectory(PLUGIN_NAME_I18N);

  VNSIServer = this;
  return true;
}

bool cPluginVNSIServer::Start(void)
{
  INFOLOG("Starting vnsi server at port=%d\n", VNSIServerConfig.listen_port);
  Server = new cVNSIServer(VNSIServerConfig.listen_port);

  return true;
}

void cPluginVNSIServer::Stop(void)
{
  delete Server;
  Server = NULL;
}

void cPluginVNSIServer::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

void cPluginVNSIServer::MainThreadHook(void)
{
  // Perform actions in the context of the main program thread.
  // WARNING: Use with great care - see PLUGINS.html!
}

cString cPluginVNSIServer::Active(void)
{
  // Return a message string if shutdown should be postponed
  return NULL;
}

time_t cPluginVNSIServer::WakeupTime(void)
{
  // Return custom wakeup time for shutdown script
  return 0;
}

cMenuSetupPage *cPluginVNSIServer::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return new cMenuSetupVNSI;
}

bool cPluginVNSIServer::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  if (!strcasecmp(Name, CONFNAME_PMTTIMEOUT))
    PmtTimeout = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_TIMESHIFT))
    TimeshiftMode = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_TIMESHIFTBUFFERSIZE))
    TimeshiftBufferSize = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_TIMESHIFTBUFFERFILESIZE))
    TimeshiftBufferFileSize = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_TIMESHIFTBUFFERDIR))
  {
    strn0cpy(TimeshiftBufferDir, Value, sizeof(TimeshiftBufferDir));
    if (*TimeshiftBufferDir && TimeshiftBufferDir[strlen(TimeshiftBufferDir)-1] == '/')
      /* strip trailing slash */
      TimeshiftBufferDir[strlen(TimeshiftBufferDir)-1] = 0;
  } else if (!strcasecmp(Name, CONFNAME_PLAYRECORDING))
    PlayRecording = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_AVOIDEPGSCAN))
    AvoidEPGScan = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_DISABLESCRAMBLETIMEOUT))
    DisableScrambleTimeout = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_DISABLECAMBLACKLIST))
    DisableCamBlacklist = atoi(Value);
  else
    return false;
  return true;
}

bool cPluginVNSIServer::Service(const char *Id, void *Data)
{
  // Handle custom service requests from other plugins
  return false;
}

const char **cPluginVNSIServer::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  return NULL;
}

cString cPluginVNSIServer::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  return NULL;
}

void cPluginVNSIServer::StoreSetup(const char *Name, int Value)
{
  if (VNSIServer)
  {
    if (VNSIServer->SetupParse(Name, itoa(Value)))
    {
      VNSIServer->SetupStore(Name, Value);
      Setup.Save();
    }
  }
}

bool cDvbVsniDeviceProbe::Probe(int Adapter, int Frontend)
{
  if (VNSIServerConfig.device)
  {
    new cDvbVnsiDevice(Adapter, Frontend);
    return true;
  }
  else
    return false;
}

cDvbVnsiDevice::cDvbVnsiDevice(int Adapter, int Frontend) :cDvbDevice(Adapter, Frontend)
{
  VNSIServerConfig.pDevice = this;
  m_hasDecoder = false;
}

cDvbVnsiDevice::~cDvbVnsiDevice()
{

}

bool cDvbVnsiDevice::HasDecoder(void) const
{
  cMutexLock lock((cMutex*)&m_mutex);
  return m_hasDecoder;
}

int cDvbVnsiDevice::PlayVideo(const uchar *Data, int Length)
{
  return Length;
}

int cDvbVnsiDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
  return Length;
}

void cDvbVnsiDevice::ActivateDecoder(bool active)
{
  cMutexLock lock(&m_mutex);
  m_hasDecoder = active;
}

VDRPLUGINCREATOR(cPluginVNSIServer); // Don't touch this!
