/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2007 Chris Tallon
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

#ifndef VNSI_REQUESTPACKET_H
#define VNSI_REQUESTPACKET_H

#include <stddef.h>

class cRequestPacket
{
public:
  cRequestPacket(uint32_t requestID, uint32_t opcode, uint8_t* data, size_t dataLength);
  ~cRequestPacket();

  size_t    getDataLength() const { return userDataLength; }
  uint32_t  getChannelID() const { return channelID; }
  uint32_t  getRequestID() const { return requestID; }
  uint32_t  getStreamID() const { return streamID; }
  uint32_t  getFlag() const { return flag; }
  uint32_t  getOpCode() const { return opCode; }

  char*     extract_String();
  uint8_t   extract_U8();
  uint32_t  extract_U32();
  uint64_t  extract_U64();
  int64_t   extract_S64();
  int32_t   extract_S32();
  double    extract_Double();

  bool      end() const;

  // If you call this, the memory becomes yours. Free with free()
  uint8_t* getData();

private:
  uint8_t* userData;
  size_t userDataLength;
  size_t packetPos;
  uint32_t opCode;

  uint32_t channelID;

  uint32_t requestID;
  uint32_t streamID;

  uint32_t flag; // stream only
};

#endif // VNSI_REQUESTPACKET_H
