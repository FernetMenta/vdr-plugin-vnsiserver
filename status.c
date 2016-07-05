/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2005-2014 Team XBMC
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

#include "status.h"
#include "vnsi.h"
#include "vnsiclient.h"

#include <vdr/tools.h>
#include <vdr/recording.h>
#include <vdr/videodir.h>
#include <vdr/shutdown.h>

cVNSIStatus::cVNSIStatus() : cThread("VNSIStatus")
{
}

cVNSIStatus::~cVNSIStatus()
{
  Shutdown();
}

void cVNSIStatus::Init(CVNSITimers *timers)
{
  m_vnsiTimers = timers;
  Start();
}

void cVNSIStatus::Shutdown()
{
  Cancel(5);
  cMutexLock lock(&m_mutex);
  for (auto *i : m_clients)
  {
    delete i;
  }
  m_clients.clear();
}

void cVNSIStatus::AddClient(cVNSIClient* client)
{
  cMutexLock lock(&m_mutex);
  m_clients.push_back(client);
}

void cVNSIStatus::Action(void)
{
  cTimeMs chanTimer(0);
  cTimeMs epgTimer(0);

  // get initial state of the recordings
#if VDRVERSNUM >= 20301
  cStateKey chanState;
  const cChannels *channels = cChannels::GetChannelsRead(chanState);
  chanState.Remove(false);
#endif

  // get initial state of the recordings
#if VDRVERSNUM >= 20301
  cStateKey recState;
  const cRecordings *recordings = cRecordings::GetRecordingsRead(recState);
  recState.Remove(false);
#else
  int recState = -1;
  Recordings.StateChanged(recState);
#endif

  // get initial state of the timers
#if VDRVERSNUM >= 20301
  cStateKey timerState;
  const cTimers *timers = cTimers::GetTimersRead(timerState);
  timerState.Remove(false);
#else
  int timerState = -1;
  Timers.Modified(timerState);
#endif

  // vnsitimer
  int vnsitimerState;
  m_vnsiTimers->StateChange(vnsitimerState);

  // last update of epg
#if VDRVERSNUM >= 20301
  cStateKey epgState;
  const cSchedules *epg = cSchedules::GetSchedulesRead(epgState);
  epgState.Remove(false);
#else
  time_t epgUpdate = cSchedules::Modified();
#endif

  // delete old timeshift file
  cString cmd;
  struct stat sb;
  if ((*TimeshiftBufferDir) && stat(TimeshiftBufferDir, &sb) == 0 && S_ISDIR(sb.st_mode))
  {
    cmd = cString::sprintf("rm -f %s/*.vnsi", TimeshiftBufferDir);
  }
  else
  {
#if VDRVERSNUM >= 20102
    cmd = cString::sprintf("rm -f %s/*.vnsi", cVideoDirectory::Name());
#else
    cmd = cString::sprintf("rm -f %s/*.vnsi", VideoDirectory);
#endif
  }
  if (system(cmd) == -1)
  {
    ERRORLOG("could not create process for deleting of timeshift files");
  }

  // set thread priority
  SetPriority(1);

  while (Running())
  {
    m_mutex.Lock();

    // remove disconnected clients
    for (ClientList::iterator i = m_clients.begin(); i != m_clients.end();)
    {
      if (!(*i)->Active())
      {
        INFOLOG("Client with ID %u seems to be disconnected, removing from client list", (*i)->GetID());
        delete (*i);
        i = m_clients.erase(i);
      }
      else {
        i++;
      }
    }

    /*!
     * Don't to updates during running channel scan, KODI's PVR manager becomes
     * restarted of finished scan.
     */
    if (!cVNSIClient::InhibidDataUpdates())
    {
      // reset inactivity timeout as long as there are clients connected
      if (!m_clients.empty())
      {
        ShutdownHandler.SetUserInactiveTimeout();
      }

      // trigger clients to reload the modified channel list
      if(chanTimer.TimedOut())
      {
#if VDRVERSNUM >= 20301
        if (channels->Lock(chanState))
        {
          chanState.Remove(false);
          INFOLOG("Requesting clients to reload channel list");
          for (auto *i : m_clients)
            i->ChannelsChange();
          chanTimer.Set(5000);
        }
#else
        int modified = Channels.Modified();
        if (modified)
        {
          Channels.SetModified((modified == CHANNELSMOD_USER) ? true : false);
          INFOLOG("Requesting clients to reload channel list");
          for (auto *i : m_clients)
            i->ChannelsChange();
        }
        chanTimer.Set(5000);
#endif
      }


#if VDRVERSNUM >= 20301
      if (recordings->Lock(recState))
      {
        recState.Remove();
        INFOLOG("Requesting clients to reload recordings list");
        for (auto *i : m_clients)
        {
          i->RecordingsChange();
        }
      }

      if (timers->Lock(timerState))
      {
        timerState.Remove(false);
        INFOLOG("Requesting clients to reload timers");
        for (auto *i : m_clients)
        {
          i->SignalTimerChange();
        }
      }

      if (m_vnsiTimers->StateChange(vnsitimerState))
      {
        INFOLOG("Requesting clients to reload vnsi-timers");
        for (auto *i : m_clients)
        {
          i->SignalTimerChange();
        }
      }

      if (epgTimer.TimedOut())
      {
        if (epg->Lock(epgState))
        {
          epgState.Remove(false);
          INFOLOG("Requesting clients to load epg");
          bool callAgain = false;
          for (auto *i : m_clients)
          {
            callAgain |= i->EpgChange();
          }
          if (callAgain)
          {
            epgTimer.Set(100);
            epgState.Reset();
          }
          else
          {
            epgTimer.Set(5000);
            m_vnsiTimers->Scan();
          }
        }
      }
#else
      // update recordings
      if(Recordings.StateChanged(recState))
      {
        INFOLOG("Recordings state changed (%i)", recState);
        INFOLOG("Requesting clients to reload recordings list");
        for (auto *i : m_clients)
          i->RecordingsChange();
      }

      // update timers
      if(Timers.Modified(timerState))
      {
        INFOLOG("Timers state changed (%i)", timerState);
        INFOLOG("Requesting clients to reload timers");
        for (auto *i : m_clients)
        {
          i->SignalTimerChange();
        }
      }

      // update epg
      if((cSchedules::Modified() > epgUpdate + 10) || time(NULL) > epgUpdate + 300)
      {
        for (auto *i : m_clients)
        {
          i->EpgChange();
        }
        epgUpdate = time(NULL);
      }
#endif
    }

    m_mutex.Unlock();

    usleep(250*1000);
  }
}
