/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2003-2006 Petri Hintukainen
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2011 Alexander Pipelka
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

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <vdr/thread.h>
#include <vdr/tools.h>

class cxSocket
{
  int m_fd;
  cMutex m_MutexWrite;
  cPoller m_pollerRead;
  cPoller m_pollerWrite;

 public:
  cxSocket(int h);
  virtual ~cxSocket();

  cxSocket(const cxSocket &) = delete;
  cxSocket &operator=(const cxSocket &) = delete;

  void Shutdown(void);
  void LockWrite();
  void UnlockWrite();
  int GetHandle();
  void Invalidate();
  ssize_t read(void *buffer, size_t size, int timeout_ms = -1);
  ssize_t write(const void *buffer, size_t size, int timeout_ms = -1, bool more_data = false);
  static char *ip2txt(uint32_t ip, unsigned int port, char *str);
};

