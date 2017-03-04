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

      // searchstring
      line = line.substr(pos+1);
      pos = line.find(";");
      if (pos == line.npos)
      {
        timer.m_search = line.substr(pos+1);
      }
      else
      {
        timer.m_search = line.substr(0, pos);
      }

      // created timers
      if (pos != line.npos)
      {
        line = line.substr(pos+1);
        while ((pos = line.find(",")) != line.npos)
        {
          std::string tmp = line.substr(0, pos);
          time_t starttime = strtol(tmp.c_str(), &pend, 10);
          timer.m_timersCreated.push_back(starttime);
          line = line.substr(pos+1);
        }
      }

      timer.m_id = ++m_nextId;
      m_timers.emplace_back(std::move(timer));
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
            << timer.m_search << ';';

      for (auto &starttime : timer.m_timersCreated)
      {
         wfile << starttime << ',';
      }
      wfile << '\n';
    }
    wfile.close();
  }
}

void CVNSITimers::Add(CVNSITimer &&timer)
{
  const cChannel *channel = FindChannelByUID(timer.m_channelUID);
  if (!channel)
    return;

  timer.m_channelID = channel->GetChannelID();
  timer.m_id = ++m_nextId;

  cMutexLock lock(&m_timerLock);
  m_timers.emplace_back(std::move(timer));
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

bool CVNSITimers::GetTimer(int id, CVNSITimer &timer)
{
  cMutexLock lock(&m_timerLock);
  id &= ~VNSITIMER_MASK;

  for (auto &searchtimer : m_timers)
  {
    if (searchtimer.m_id == id)
    {
      timer = searchtimer;
      return true;
    }
  }
  return false;
}

bool CVNSITimers::UpdateTimer(int id, CVNSITimer &timer)
{
  cMutexLock lock(&m_timerLock);
  id &= ~VNSITIMER_MASK;

  for (auto &searchtimer : m_timers)
  {
    if (searchtimer.m_id == id)
    {
      const cChannel *channel = FindChannelByUID(timer.m_channelUID);
      if (!channel)
        return false;

      if (timer.m_channelUID != searchtimer.m_channelUID ||
          timer.m_search != searchtimer.m_search)
      {
        DeleteChildren(searchtimer);
      }

      timer.m_id = id;
      timer.m_channelID = channel->GetChannelID();
      timer.m_timersCreated = searchtimer.m_timersCreated;

      searchtimer = timer;
      m_state++;
      Save();
      return true;
    }
  }
  return false;
}

bool CVNSITimers::DeleteTimer(int id)
{
  cMutexLock lock(&m_timerLock);
  id &= ~VNSITIMER_MASK;

  std::vector<CVNSITimer>::iterator it;
  for (it = m_timers.begin(); it != m_timers.end(); ++it)
  {
    if (it->m_id == id)
    {
      DeleteChildren(*it);
      m_timers.erase(it);
      m_state++;
      Save();
      return true;
    }
  }
  return false;
}

void CVNSITimers::DeleteChildren(CVNSITimer &vnsitimer)
{
#if VDRVERSNUM >= 20301
  cStateKey timerState;
  timerState.Reset();
  bool modified = false;
  cTimers *Timers = cTimers::GetTimersWrite(timerState);
  if (Timers)
  {
    Timers->SetExplicitModify();
    cTimer *timer = Timers->First();
    while (timer)
    {
      if (!timer->Channel())
        continue;

      timer->Matches();
      cTimer* nextTimer = Timers->Next(timer);
      for (auto &starttime : vnsitimer.m_timersCreated)
      {
        if (vnsitimer.m_channelID == timer->Channel()->GetChannelID() &&
            timer->StartTime() == starttime &&
            !timer->Recording())
        {
          Timers->Del(timer);
          Timers->SetModified();
          modified = true;
          break;
        }
      }
      timer = nextTimer;
    }
    timerState.Remove(modified);
    vnsitimer.m_timersCreated.clear();
  }
#endif
}

int CVNSITimers::GetParent(const cTimer *timer)
{
  if (!timer->Channel())
    return 0;

  timer->Matches();
  cMutexLock lock(&m_timerLock);
  for (auto &searchTimer : m_timers)
  {
    if (searchTimer.m_channelID == timer->Channel()->GetChannelID())
    {
      for (auto &starttime : searchTimer.m_timersCreated)
      {
        if (timer->StartTime() == starttime)
        {
          return searchTimer.m_id | VNSITIMER_MASK;
        }
      }
    }
  }
  return 0;
}

bool CVNSITimers::IsChild(int id, time_t starttime)
{
  cMutexLock lock(&m_timerLock);

  for (auto &timer : m_timers)
  {
    if (timer.m_id != id)
      continue;

    for (auto &time : timer.m_timersCreated)
    {
      if (time == starttime)
        return true;
    }
  }
  return false;
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
        strcmp(timerEvent->Title(), event->Title()) == 0)
    {
      if (timerEvent->ShortText() != nullptr && event->ShortText() != nullptr &&
          strcmp(timerEvent->ShortText(), event->ShortText()) == 0)
        return true;

      if (abs(difftime(timerEvent->StartTime(), event->StartTime())) < 300)
        return true;
    }
  }
  return false;
}

void CVNSITimers::Action()
{
#if VDRVERSNUM >= 20301
  bool modified;
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
            if (event->EndTime() < time(nullptr))
              continue;

            std::string title(event->Title());
            std::smatch m;
            std::regex e(Convert(searchTimer.m_search));

            if (std::regex_search(title, m, e, std::regex_constants::match_not_null))
            {
              bool duplicate = false;
              LOCK_RECORDINGS_READ;
              for (const cRecording *recording = Recordings->First(); recording; recording = Recordings->Next(recording))
              {
                if (recording->Info() != nullptr)
                {
                  if (recording->Info()->Title() != nullptr &&
                      strcmp(recording->Info()->Title(), event->Title()) == 0)
                  {
                    if (recording->Info()->ShortText() != nullptr && event->ShortText() != nullptr &&
                        strcmp(recording->Info()->ShortText(), event->ShortText()) == 0)
                    {
                      duplicate = true;
                      break;
                    }
                  }
                }
                if (abs(difftime(event->StartTime(), recording->Start())) < 300)
                {
                  duplicate = true;
                  break;
                }
              }
              if (duplicate)
                continue;

              if (IsDuplicateEvent(Timers, event))
                continue;

              cTimer *newTimer = new cTimer(event);

              if (!Setup.MarginStart)
              {
                unsigned int start = newTimer->Start();
                if (start < searchTimer.m_marginStart)
                {
                  newTimer->SetDay(cTimer::IncDay(newTimer->Day(), -1));
                  start = 24*3600 - (searchTimer.m_marginStart - start);
                }
                else
                  start -= searchTimer.m_marginStart;
                newTimer->SetStart(start);
              }

              if (!Setup.MarginStop)
              {
                unsigned int stop = newTimer->Stop();
                if (stop + searchTimer.m_marginEnd >= 24*3600)
                {
                  newTimer->SetDay(cTimer::IncDay(newTimer->Day(), 1));
                  stop = stop + searchTimer.m_marginEnd - 24*3600;
                }
                else
                  stop += searchTimer.m_marginEnd;
                newTimer->SetStop(stop);
              }

              if (IsChild(searchTimer.m_id, newTimer->StartTime()))
              {
                delete newTimer;
                continue;
              }

              Timers->Add(newTimer);
              modified = true;

              {
                cMutexLock lock(&m_timerLock);
                for (auto &origTimer : m_timers)
                {
                  if (origTimer.m_id == searchTimer.m_id)
                  {
                    origTimer.m_timersCreated.push_back(newTimer->StartTime());
                    Save();
                    break;
                  }
                }
              }
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
