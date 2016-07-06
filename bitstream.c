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

#include "bitstream.h"

cBitstream::cBitstream(uint8_t *data, int bits)
  :m_data(data), m_len(bits)
{
}

// this is a bitstream that has embedded emulation_prevention_three_byte
// sequences that need to be removed as used in HECV.
// Data must start at byte 2 

cBitstream::cBitstream(uint8_t *data, unsigned int bits, bool doEP3)
  :m_data(data),
   m_offset(16), // skip header and use as sentinel for EP3 detection
   m_len(bits),
   m_doEP3(true)
{
}

void cBitstream::skipBits(int num)
{
  if (m_doEP3)
  {
    register unsigned int tmp;

    while (num)
    {
      tmp = m_offset >> 3;
      if (!(m_offset & 7) && (m_data[tmp--] == 3) && (m_data[tmp--] == 0) && (m_data[tmp] == 0))
        m_offset += 8;   // skip EP3 byte

      if (!(m_offset & 7) && (num >= 8)) // byte boundary, speed up things a little bit
      {
        m_offset += 8;
        num -= 8;
      }
      else if ((tmp = 8-(m_offset & 7)) <= num) // jump to byte boundary
      {
        m_offset += tmp;
        num -= tmp;
      }
      else
      {
        m_offset += num;
         num = 0;
      }

      if (m_offset >= m_len)
      {
        m_error = true;
        break;
      }
    }

    return;
  }

  m_offset += num;
}

unsigned int cBitstream::readBits(int num)
{
  int r = 0;

  while(num > 0)
  {
    if (m_doEP3)
    {
      register unsigned int tmp = m_offset >> 3;
      if (!(m_offset & 7) && (m_data[tmp--] == 3) && (m_data[tmp--] == 0) && (m_data[tmp] == 0))
        m_offset += 8;   // skip EP3 byte
    }

    if(m_offset >= m_len)
    {
      m_error = true;
      return 0;
    }

    num--;

    if(m_data[m_offset / 8] & (1 << (7 - (m_offset & 7))))
      r |= 1 << num;

    m_offset++;
  }
  return r;
}

unsigned int cBitstream::showBits(int num)
{
  int r = 0;
  int offs = m_offset;

  while(num > 0)
  {
    if(offs >= m_len)
    {
      m_error = true;
      return 0;
    }

    num--;

    if(m_data[offs / 8] & (1 << (7 - (offs & 7))))
      r |= 1 << num;

    offs++;
  }
  return r;
}

unsigned int cBitstream::readGolombUE(int maxbits)
{
  int lzb = -1;
  int bits = 0;

  for(int b = 0; !b; lzb++, bits++)
  {
    if (bits > maxbits)
      return 0;
    b = readBits1();
  }

  return (1 << lzb) - 1 + readBits(lzb);
}

signed int cBitstream::readGolombSE()
{
  int v, pos;
  v = readGolombUE();
  if(v == 0)
    return 0;

  pos = (v & 1);
  v = (v + 1) >> 1;
  return pos ? v : -v;
}
