/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2005-2013 Team XBMC
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

#pragma once

#include <string>
#include <vector>
#include <set>
#include <vdr/thread.h>
#include <vdr/channels.h>

class cVNSIProvider
{
public:
  cVNSIProvider();
  cVNSIProvider(std::string name, int caid);
  bool operator==(const cVNSIProvider &rhs) const;
  std::string m_name;
  int m_caid;
};

class cVNSIChannelFilter
{
public:
  void Load();
  void StoreWhitelist(bool radio);
  void StoreBlacklist(bool radio);
  bool IsWhitelist(const cChannel &channel);
  bool PassFilter(const cChannel &channel);
  void SortChannels();
  static bool IsRadio(const cChannel* channel);
  std::vector<cVNSIProvider> m_providersVideo;
  std::vector<cVNSIProvider> m_providersRadio;
  std::set<int> m_channelsVideo;
  std::set<int> m_channelsRadio;
  cMutex m_Mutex;
};

extern cVNSIChannelFilter VNSIChannelFilter;
