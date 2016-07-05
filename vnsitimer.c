/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2005-2016 Team Kodi
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

#include "config.h"
#include "hash.h"
#include "time.h"
#include <vdr/tools.h>
#include <vdr/epg.h>
#include <vdr/timers.h>
#include <vdr/recording.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <regex>
#include "vnsitimer.h"

CVNSITimers::CVNSITimers() : cThread("VNSITimers")
{
  m_doScan = false;
  m_state = 0;
}

void CVNSITimers::Load()
{
  cString filename;
  std::string line;
  std::ifstream rfile;
  CVNSITimer timer;

  cMutexLock lock(&m_timerLock);

  filename = cString::sprintf("%s/timers.vnsi", *VNSIServerConfig.ConfigDirectory);
  rfile.open(filename);
  m_timers.clear();
  if (rfile.is_open())
  {
    while(getline(rfile,line))
    {
      // timer name
      size_t pos = line.find(";");
      if(pos == line.npos)
      {
        continue;
      }
      timer.m_name = line.substr(0, pos);

      // channelUID
      line = line.substr(pos+1);
      pos = line.find(";");
      if(pos == line.npos)
      {
        continue;
      }
      char *pend;
      std::string channeluid = line.substr(0, pos);
      timer.m_channelUID = strtol(channeluid.c_str(), &pend, 10);

      const cChannel *channel = FindChannelByUID(timer.m_channelUID);
      if (!channel)
      {
        continue;
      }
      timer.m_channelID = channel->GetChannelID();

      // enabled
      line = line.substr(pos+1);
      pos = line.find(";");
      if(pos == line.npos)
      {
        continue;
      }
      std::string enabled = line.substr(0, pos);
      timer.m_enabled = strtol(enabled.c_str(), &pend, 10);

      // priority
      line = line.substr(pos+1);
      pos = line.find(";");
      if(pos == line.npos)
      {
        continue;
      }
      std::string priority = line.substr(0, pos);
      timer.m_priority = strtol(priority.c_str(), &pend, 10);

      // lifetime
      line = line.substr(pos+1);
      pos = line.find(";");
      if(pos == line.npos)
      {
        continue;
      }
      std::string lifetime = line.substr(0, pos);
      timer.m_lifetime = strtol(lifetime.c_str(), &pend, 10);

      timer.m_search = line.substr(pos+1);

      m_timers.push_back(timer);
    }
    rfile.close();
  }
}

void CVNSITimers::Save()
{
  cString filename;
  std::ofstream wfile;
  filename = cString::sprintf("%s/timers.vnsi", *VNSIServerConfig.ConfigDirectory);

  cMutexLock lock(&m_timerLock);

  wfile.open(filename);
  if(wfile.is_open())
  {
    for (auto &timer : m_timers)
    {
      wfile << timer.m_name << ';'
            << timer.m_channelUID << ';'
            << timer.m_enabled << ';'
            << timer.m_priority << ';'
            << timer.m_lifetime << ';'
            << timer.m_search << '\n';
    }
    wfile.close();
  }
}

void CVNSITimers::Add(CVNSITimer &timer)
{
  const cChannel *channel = FindChannelByUID(timer.m_channelUID);
  if (!channel)
    return;

  timer.m_channelID = channel->GetChannelID();

  cMutexLock lock(&m_timerLock);
  m_timers.push_back(timer);
  m_state++;

  Save();
}

void CVNSITimers::Scan()
{
  m_doScan = true;
}

size_t CVNSITimers::GetTimersCount()
{
  cMutexLock lock(&m_timerLock);
  return m_timers.size();
}

std::vector<CVNSITimer> CVNSITimers::GetTimers()
{
  cMutexLock lock(&m_timerLock);
  return m_timers;
}

bool CVNSITimers::GetTimer(int idx, CVNSITimer &timer)
{
  cMutexLock lock(&m_timerLock);
  idx &= ~INDEX_MASK;
  if (idx < 0 || idx >= (int)m_timers.size())
    return false;
  timer = m_timers[idx];
  return true;
}

bool CVNSITimers::UpdateTimer(int idx, CVNSITimer &timer)
{
  cMutexLock lock(&m_timerLock);
  idx &= ~INDEX_MASK;
  if (idx < 0 || idx >= (int)m_timers.size())
    return false;
  m_timers[idx] = timer;
  m_state++;
  Save();
  return true;
}

bool CVNSITimers::DeleteTimer(int idx)
{
  cMutexLock lock(&m_timerLock);
  idx &= ~INDEX_MASK;
  if (idx < 0 || idx >= (int)m_timers.size())
    return false;
  m_timers.erase(m_timers.begin()+idx);
  m_state++;
  Save();
  return true;
}

bool CVNSITimers::StateChange(int &state)
{
  if (state != m_state)
  {
    state = m_state;
    return true;
  }
  return false;
}

std::string CVNSITimers::Convert(std::string search)
{
  std::string regex;
  size_t pos = search.find("*");
  while (pos != search.npos)
  {
    regex += search.substr(0, pos);
    regex += ".*";
    search = search.substr(pos + 1);
    pos = search.find("*");
  }
  regex += search;
  return regex;
}

bool CVNSITimers::IsDuplicateEvent(cTimers *timers, const cEvent *event)
{
  for (const cTimer *timer = timers->First(); timer; timer = timers->Next(timer))
  {
    const cEvent *timerEvent = timer->Event();
    if (timerEvent == nullptr)
      continue;
    if (timer->HasFlags(tfActive) &&
        strcmp(timerEvent->Title(), event->Title()) == 0 &&
        strcmp(timerEvent->ShortText(), event->ShortText()) == 0)
      return true;
  }
  return false;
}

void CVNSITimers::Action()
{
  bool modified;

#if VDRVERSNUM >= 20301
  cStateKey timerState;

  // set thread priority (nice level)
  SetPriority(1);

  while (Running())
  {
    if (!m_doScan)
    {
      usleep(1000*1000);
      continue;
    }
    m_doScan = false;

    std::vector<CVNSITimer> timers;
    {
      cMutexLock lock(&m_timerLock);
      timers = m_timers;
    }

    cTimers *Timers = cTimers::GetTimersWrite(timerState);
    if (!Timers)
      continue;

    Timers->SetExplicitModify();
    modified = false;
    cStateKey SchedulesStateKey(true);
    const cSchedules *schedules = cSchedules::GetSchedulesRead(SchedulesStateKey);
    if (schedules)
    {
      for (const cSchedule *schedule = schedules->First(); schedule; schedule = schedules->Next(schedule))
      {
        for (auto &searchTimer : timers)
        {
          if (!searchTimer.m_enabled)
            continue;

          if (!(searchTimer.m_channelID == schedule->ChannelID()))
            continue;

          for (const cEvent *event = schedule->Events()->First(); event; event = schedule->Events()->Next(event))
          {
            std::string title(event->Title());
            std::smatch m;
            std::regex e(Convert(searchTimer.m_search));

            if (std::regex_search(title, m, e,  std::regex_constants::match_not_null))
            {
              bool duplicate = false;
              LOCK_RECORDINGS_READ;
              for (const cRecording *recording = Recordings->First(); recording; recording = Recordings->Next(recording))
              {
                if (recording->Info() != nullptr)
                {
                  if (strcmp(recording->Info()->Title(), event->Title()) == 0 &&
                      strcmp(recording->Info()->ShortText(), event->ShortText()) == 0)
                  {
                    duplicate = true;
                    break;
                  }
                }
                if (difftime(event->StartTime(), recording->Start()) < 300)
                {
                  duplicate = true;
                  break;
                }
              }
              if (duplicate)
                continue;

              if (IsDuplicateEvent(Timers, event))
                continue;

              std::unique_ptr<cTimer> newTimer(new cTimer(event));
              Timers->Add(newTimer.release());
              modified = true;
            }
          }
        }
      }
    }
    if (modified)
      Timers->SetModified();
    timerState.Remove(modified);
    SchedulesStateKey.Remove(modified);
  }

#endif
}

void CVNSITimers::Shutdown()
{
  Cancel(5);
  m_timers.clear();
}
