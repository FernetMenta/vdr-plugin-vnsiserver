/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2005-2012 Team XBMC
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

#include "setup.h"
#include "vnsicommand.h"

int PmtTimeout = 0;
int TimeshiftMode = 0;
int TimeshiftBufferSize = 5;
int TimeshiftBufferFileSize = 6;
char TimeshiftBufferDir[PATH_MAX] = "\0";
int PlayRecording = 0;
int GroupRecordings = 1;
int AvoidEPGScan = 1;
int DisableScrambleTimeout = 0;
int DisableCamBlacklist = 0;
int EdlMode = 0;

cMenuSetupVNSI::cMenuSetupVNSI(void)
{
  timeshiftModesTexts[0] = tr("Off");
  timeshiftModesTexts[1] = tr("RAM");
  timeshiftModesTexts[2] = tr("File");
  newTimeshiftMode = TimeshiftMode;
  Add(new cMenuEditStraItem( tr("Time Shift Mode"), &newTimeshiftMode, 3, timeshiftModesTexts));

  newTimeshiftBufferSize = TimeshiftBufferSize;
  Add(new cMenuEditIntItem( tr("TS Buffersize (RAM) (1-80) x 100MB"), &newTimeshiftBufferSize));

  newTimeshiftBufferFileSize = TimeshiftBufferFileSize;
  Add(new cMenuEditIntItem( tr("TS Buffersize (File) (1-10) x 1GB"), &newTimeshiftBufferFileSize));

  strn0cpy(newTimeshiftBufferDir, TimeshiftBufferDir, sizeof(newTimeshiftBufferDir));
  Add(new cMenuEditStrItem(tr("TS Buffer Directory"), newTimeshiftBufferDir, sizeof(newTimeshiftBufferDir)));

  newPlayRecording = PlayRecording;
  Add(new cMenuEditBoolItem( tr("Play Recording instead of live"), &newPlayRecording));

  newGroupRecordings = GroupRecordings;
  Add(new cMenuEditBoolItem( tr("Group series recordings"), &newGroupRecordings));

  newAvoidEPGScan = AvoidEPGScan;
  Add(new cMenuEditBoolItem( tr("Avoid EPG scan while streaming"), &newAvoidEPGScan));

  newDisableScrambleTimeout = DisableScrambleTimeout;
  Add(new cMenuEditBoolItem( tr("Disable scramble timeout"), &newDisableScrambleTimeout));

  newDisableCamBlacklist = DisableCamBlacklist;
  Add(new cMenuEditBoolItem( tr("Disable cam blacklist"), &newDisableCamBlacklist));

  edlModesTexts[0] = tr("scene");
  edlModesTexts[1] = tr("comskip");
  edlModesTexts[2] = tr("cut");
  newEdlMode = EdlMode;
  Add(new cMenuEditStraItem( tr("EDL Mode"), &newEdlMode, 3, edlModesTexts));
}

void cMenuSetupVNSI::Store(void)
{
  if (newPmtTimeout > 10 || newPmtTimeout < 0)
    newPmtTimeout = 2;
  SetupStore(CONFNAME_PMTTIMEOUT, PmtTimeout = newPmtTimeout);

  SetupStore(CONFNAME_TIMESHIFT, TimeshiftMode = newTimeshiftMode);

  if (newTimeshiftBufferSize > 80)
    newTimeshiftBufferSize = 80;
  else if (newTimeshiftBufferSize < 1)
    newTimeshiftBufferSize = 1;
  SetupStore(CONFNAME_TIMESHIFTBUFFERSIZE, TimeshiftBufferSize = newTimeshiftBufferSize);

  if (newTimeshiftBufferFileSize > 20)
    newTimeshiftBufferFileSize = 20;
  else if (newTimeshiftBufferFileSize < 1)
    newTimeshiftBufferFileSize = 1;
  SetupStore(CONFNAME_TIMESHIFTBUFFERFILESIZE, TimeshiftBufferFileSize = newTimeshiftBufferFileSize);

  strn0cpy(TimeshiftBufferDir, newTimeshiftBufferDir, sizeof(TimeshiftBufferDir));
  if (*TimeshiftBufferDir && TimeshiftBufferDir[strlen(TimeshiftBufferDir)-1] == '/')
    /* strip trailing slash */
    TimeshiftBufferDir[strlen(TimeshiftBufferDir)-1] = 0;

  SetupStore(CONFNAME_TIMESHIFTBUFFERDIR, TimeshiftBufferDir);

  SetupStore(CONFNAME_PLAYRECORDING, PlayRecording = newPlayRecording);

  SetupStore(CONFNAME_GROUPRECORDINGS, GroupRecordings = newGroupRecordings);

  SetupStore(CONFNAME_AVOIDEPGSCAN, AvoidEPGScan = newAvoidEPGScan);

  SetupStore(CONFNAME_DISABLESCRAMBLETIMEOUT, DisableScrambleTimeout = newDisableScrambleTimeout);

  SetupStore(CONFNAME_DISABLECAMBLACKLIST, DisableCamBlacklist = newDisableCamBlacklist);

  SetupStore(CONFNAME_EDL, EdlMode = newEdlMode);
}
