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

#pragma once

#include <memory>
#include <atomic>
#include <vdr/channels.h>
#include <vdr/thread.h>

class cLivePatFilter;
class cLiveReceiver;
class cVideoBuffer;
class cDevice;
class cDummyReceiver;

class cVideoInput
{
friend class cLivePatFilter;
friend class cLiveReceiver;
public:
  cVideoInput(cCondWait &event);
  virtual ~cVideoInput();
  bool Open(const cChannel *channel, int priority, cVideoBuffer *videoBuffer);
  void Close();
  bool IsOpen();
  void RequestRetune();
  enum eReceivingStatus {NORMAL, RETUNE, CLOSE};
  eReceivingStatus ReceivingStatus();
protected:
  cChannel *PmtChannel();
  void Receive(const uchar *data, int length);
  cDevice          *m_Device;
  cLivePatFilter   *m_PatFilter;
  cLiveReceiver    *m_Receiver;
  const cChannel   *m_Channel;
  cVideoBuffer     *m_VideoBuffer;
  int               m_Priority;
  std::atomic<bool> m_PmtChange;
  cChannel m_PmtChannel;
  cCondWait &m_Event;
  std::shared_ptr<cDummyReceiver> m_DummyReceiver;
  std::atomic<bool> m_RetuneRequested;
};
