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

#include <getopt.h>
#include <vdr/plugin.h>
#include <vdr/thread.h>
#include "vnsiserver.h"

static const char *VERSION        = "1.6.0";
static const char *DESCRIPTION    = "VDR-Network-Streaming-Interface (VNSI) Server";

extern int PmtTimeout;
extern int TimeshiftMode;
extern int TimeshiftBufferSize;
extern int TimeshiftBufferFileSize;
extern char TimeshiftBufferDir[PATH_MAX];
extern int PlayRecording;
extern int GroupRecordings;
extern int AvoidEPGScan;
extern int DisableScrambleTimeout;
extern int DisableCamBlacklist;
extern int EdlMode;

class cDvbVsniDeviceProbe : public cDvbDeviceProbe
{
public:
  virtual bool Probe(int Adapter, int Frontend);
};

class cPluginVNSIServer : public cPlugin {
private:
  cVNSIServer *Server;
  static cPluginVNSIServer *VNSIServer;
  cDvbVsniDeviceProbe probe;

public:
  cPluginVNSIServer(void);
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return DESCRIPTION; }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual void Housekeeping(void);
  virtual void MainThreadHook(void);
  virtual cString Active(void);
  virtual time_t WakeupTime(void);
  virtual const char *MainMenuEntry(void) { return NULL; }
  virtual cOsdObject *MainMenuAction(void) { return NULL; }
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data = NULL);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);

  static void StoreSetup(const char *Name, int Value);
};

class cDvbVnsiDevice : public cDvbDevice
{
public:
  cDvbVnsiDevice(int Adapter, int Frontend);
  virtual ~cDvbVnsiDevice();
  virtual bool HasDecoder(void) const;
  int PlayVideo(const uchar *Data, int Length);
  int PlayAudio(const uchar *Data, int Length, uchar Id);
  void ActivateDecoder(bool active);
protected:
  bool m_hasDecoder;
  cMutex m_mutex;
};
