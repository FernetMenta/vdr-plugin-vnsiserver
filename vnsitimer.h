/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2005-2016 Team XBMC
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

#pragma once

#include "stdint.h"
#include <string>
#include <vector>
#include <atomic>
#include <vdr/channels.h>
#include <vdr/thread.h>

class cEvent;
class cTimers;

class CVNSITimer
{
public:
  std::string m_name;
  uint32_t m_channelUID;
  int32_t m_enabled;
  int32_t m_priority;
  int32_t m_lifetime;
  std::string m_search;
  tChannelID m_channelID;
};

class CVNSITimers : public cThread
{
public:
  CVNSITimers();
  void Load();
  void Save();
  void Shutdown();
  void Add(CVNSITimer &timer);
  void Scan();
  size_t GetTimersCount();
  bool StateChange(int &state);
  std::vector<CVNSITimer> GetTimers();
  bool GetTimer(int idx, CVNSITimer &timer);
  bool UpdateTimer(int idx, CVNSITimer &timer);
  bool DeleteTimer(int idx);

  static constexpr uint32_t INDEX_MASK = 0xF0000000;
protected:
  virtual void Action(void) override;
  std::string Convert(std::string search);
  bool IsDuplicateEvent(cTimers *timers, const cEvent *event);

  std::vector<CVNSITimer> m_timers;
  std::atomic_bool m_doScan;
  std::atomic_int m_state;
  cMutex m_timerLock;
};

