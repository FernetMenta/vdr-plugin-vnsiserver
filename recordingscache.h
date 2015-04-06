/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
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

#ifndef VNSI_RECORDINGSCACHE_H
#define VNSI_RECORDINGSCACHE_H

#include <stdint.h>
#include <map>
#include <vdr/thread.h>
#include <vdr/tools.h>
#include <vdr/recording.h>

class cRecordingsCache
{
protected:

  cRecordingsCache();

  virtual ~cRecordingsCache();

public:

  static cRecordingsCache& GetInstance();

  uint32_t Register(cRecording* recording, bool deleted = false);

  cRecording* Lookup(uint32_t uid);

private:
  struct RecordingsInfo
  {
    cString filename;
    bool isDeleted;
  };
  std::map<uint32_t, RecordingsInfo> m_recordings;

  cMutex m_mutex;
};


#endif // VNSI_RECORDINGSCACHE_H
