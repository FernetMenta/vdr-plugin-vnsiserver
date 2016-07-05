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

#pragma once

#include <vdr/thread.h>
#include <list>
#include "vnsitimer.h"

class cVNSIClient;

typedef std::list<cVNSIClient*> ClientList;

class cVNSIStatus : public cThread
{
public:
  cVNSIStatus();
  virtual ~cVNSIStatus();

  cVNSIStatus(const cVNSIStatus &) = delete;
  cVNSIStatus &operator=(const cVNSIStatus &) = delete;

  void Init(CVNSITimers *timers);
  void Shutdown();
  void AddClient(cVNSIClient* client);

protected:
  virtual void Action(void);

  ClientList m_clients;
  cMutex m_mutex;
  CVNSITimers *m_vnsiTimers;
};
