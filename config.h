/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
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

#ifndef VNSI_CONFIG_H
#define VNSI_CONFIG_H

#include <string.h>
#include <stdint.h>

#include <vdr/config.h>

// log output configuration

#ifdef CONSOLEDEBUG
#define INFOLOG(x...) printf("VNSI: " x)
#define ERRORLOG(x...) printf("VNSI-Error: " x)
#define DEBUGLOG(x...) printf("VNSI-Debug: " x)
#else
#define INFOLOG(x...) isyslog("VNSI: " x)
#define ERRORLOG(x...) esyslog("VNSI-Error: " x)
#define DEBUGLOG(x...) (SysLogLevel > 3) ? dsyslog("VNSI-Debug: " x) : void()
#endif

// default settings

#define ALLOWED_HOSTS_FILE  "allowed_hosts.conf"
#define FRONTEND_DEVICE     "/dev/dvb/adapter%d/frontend%d"

#define LISTEN_PORT       34890
#define LISTEN_PORT_S    "34890"
#define DISCOVERY_PORT    34890

// backward compatibility

#if APIVERSNUM < 10701
#define FOLDERDELIMCHAR '~'
#endif

// Error flags
#define ERROR_PES_GENERAL   0x01
#define ERROR_PES_SCRAMBLE  0x02
#define ERROR_PES_STARTCODE 0x04
#define ERROR_TS_SCRAMBLE   0x08
#define ERROR_DEMUX_NODATA  0x10
#define ERROR_CAM_ERROR     0x20


class cVNSIServerConfig
{
public:
  cVNSIServerConfig();

  // Remote server settings
  cString ConfigDirectory;      // config directory path
  uint16_t listen_port;         // Port of remote server
  uint16_t stream_timeout;      // timeout in seconds for stream data
  bool device;                  // true if vnsi should act as dummy device
  void *pDevice;                // pointer to cDvbVnsiDevice
  cString testStreamFile;       // TS file to simulate channel
  bool testStreamActive;        // true if test mode is enabled
};

// Global instance
extern cVNSIServerConfig VNSIServerConfig;

#endif // VNSI_CONFIG_H
