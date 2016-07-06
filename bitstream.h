/*
 *      vdr-plugin-vnsi - KODI server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
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

#ifndef VNSI_BITSTREAM_H
#define VNSI_BITSTREAM_H

#include <stdint.h>
#include <stddef.h>

class cBitstream
{
private:
  uint8_t *const m_data;
  size_t   m_offset = 0;
  const size_t m_len;
  bool     m_error = false;
  const bool m_doEP3 = false;

public:
  constexpr cBitstream(uint8_t *data, size_t bits)
    :m_data(data), m_len(bits)
  {
  }

  // this is a bitstream that has embedded emulation_prevention_three_byte
  // sequences that need to be removed as used in HECV.
  // Data must start at byte 2
  constexpr cBitstream(uint8_t *data, size_t bits, bool doEP3)
    :m_data(data),
     m_offset(16), // skip header and use as sentinel for EP3 detection
     m_len(bits),
     m_doEP3(true)
  {
  }

  void         skipBits(int num);
  unsigned int readBits(int num);
  unsigned int showBits(int num);
  unsigned int readBits1() { return readBits(1); }
  unsigned int readGolombUE(int maxbits = 32);
  signed int   readGolombSE();
  constexpr size_t length() const { return m_len; }
  constexpr bool isError() const { return m_error; }
};

#endif // VNSI_BITSTREAM_H
