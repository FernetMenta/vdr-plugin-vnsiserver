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

#include "parser_MPEGAudio.h"
#include "bitstream.h"
#include "config.h"

#include <stdlib.h>
#include <assert.h>

#define MAX_RDS_BUFFER_SIZE 100000

const uint16_t FrequencyTable[3] = { 44100, 48000, 32000 };
const uint16_t BitrateTable[2][3][15] =
{
  {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
    {0, 32, 48, 56, 64,  80,  96,  112, 128, 160, 192, 224, 256, 320, 384 },
    {0, 32, 40, 48, 56,  64,  80,  96,  112, 128, 160, 192, 224, 256, 320 }
  },
  {
    {0, 32, 48, 56, 64,  80,  96,  112, 128, 144, 160, 176, 192, 224, 256},
    {0, 8,  16, 24, 32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160},
    {0, 8,  16, 24, 32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160}
  }
};

cParserMPEG2Audio::cParserMPEG2Audio(int pID, cTSStream *stream, sPtsWrap *ptsWrap, bool observePtsWraps, bool enableRDS)
 : cParser(pID, stream, ptsWrap, observePtsWraps)
{
  m_PTS                       = 0;
  m_DTS                       = 0;
  m_FrameSize                 = 0;
  m_SampleRate                = 0;
  m_Channels                  = 0;
  m_BitRate                   = 0;
  m_PesBufferInitialSize      = 2048;
  m_RDSEnabled                = enableRDS;
  m_RDSBufferInitialSize      = 384;
  m_RDSBuffer                 = NULL;
  m_RDSBufferSize             = 0;
  m_RDSExtPID                 = 0;
}

cParserMPEG2Audio::~cParserMPEG2Audio()
{
  free(m_RDSBuffer);
}

void cParserMPEG2Audio::Parse(sStreamPacket *pkt, sStreamPacket *pkt_side_data)
{
  int p = m_PesParserPtr;
  int l;
  while ((l = m_PesBufferPtr - p) > 3)
  {
    if (FindHeaders(m_PesBuffer + p, l) < 0)
      break;
    p++;
  }
  m_PesParserPtr = p;

  if (m_FoundFrame && l >= m_FrameSize)
  {
    bool streamChange = m_Stream->SetAudioInformation(m_Channels, m_SampleRate, m_BitRate, 0, 0);
    pkt->id       = m_pID;
    pkt->data     = &m_PesBuffer[p];
    pkt->size     = m_FrameSize;
    pkt->duration = 90000 * 1152 / m_SampleRate;
    pkt->dts      = m_DTS;
    pkt->pts      = m_PTS;
    pkt->streamChange = streamChange;

    m_PesNextFramePtr = p + m_FrameSize;
    m_PesParserPtr = 0;
    m_FoundFrame = false;

    if (m_RDSEnabled)
    {
      /*
       * Reading of RDS Universal Encoder Communication Protocol
       * If present it starts on end of a mpeg audio stream and goes
       * backwards.
       * See ETSI TS 101 154 - C.4.2.18 for details.
       */
      int rdsl = m_PesBuffer[p+m_FrameSize-2];                  // RDS DataFieldLength
      if (m_PesBuffer[p+m_FrameSize-1] == 0xfd && rdsl > 0)     // RDS DataSync = 0xfd @ end
      {
        if (m_RDSBuffer == NULL)
        {
          m_RDSBufferSize = m_RDSBufferInitialSize;
          m_RDSBuffer = (uint8_t*)malloc(m_RDSBufferSize);

          if (m_RDSBuffer == NULL)
          {
            ERRORLOG("PVR Parser MPEG2-Audio - %s - malloc failed for RDS data", __FUNCTION__);
            m_RDSEnabled = false;
            return;
          }

          m_RDSExtPID = m_Stream->AddSideDataType(scRDS);
          if (!m_RDSExtPID)
          {
            ERRORLOG("PVR Parser MPEG2-Audio - %s - failed to add RDS data stream", __FUNCTION__);
            m_RDSEnabled = false;
            return;
          }
        }

        if (rdsl >= m_RDSBufferSize)
        {
          if (rdsl >= MAX_RDS_BUFFER_SIZE)
          {
            ERRORLOG("PVR Parser MPEG2-Audio - %s - max buffer size (%i kB) for RDS data reached, pid: %d", __FUNCTION__, MAX_RDS_BUFFER_SIZE/1024, m_pID);
            m_RDSEnabled = false;
            return;
          }
          m_RDSBufferSize += m_RDSBufferInitialSize / 10;
          m_RDSBuffer = (uint8_t*)realloc(m_RDSBuffer, m_RDSBufferSize);
          if (m_RDSBuffer == NULL)
          {
            ERRORLOG("PVR Parser MPEG2-Audio - %s - realloc for RDS data failed", __FUNCTION__);
            m_RDSEnabled = false;
            return;
          }
        }

        int pes_buffer_ptr = 0;
        for (int i = m_FrameSize-3; i > m_FrameSize-3-rdsl; i--)    // <-- data reverse, from end to start
          m_RDSBuffer[pes_buffer_ptr++] = m_PesBuffer[p+i];

        pkt_side_data->id       = m_RDSExtPID;
        pkt_side_data->data     = m_RDSBuffer;
        pkt_side_data->size     = pes_buffer_ptr;
        pkt_side_data->duration = 0;
        pkt_side_data->dts      = m_curDTS;
        pkt_side_data->pts      = m_curPTS;
        pkt_side_data->streamChange = false;
      }
    }
  }
}


int cParserMPEG2Audio::FindHeaders(uint8_t *buf, int buf_size)
{
  if (m_FoundFrame)
    return -1;

  if (buf_size < 4)
    return -1;

  uint8_t *buf_ptr = buf;

  if ((buf_ptr[0] == 0xFF && (buf_ptr[1] & 0xE0) == 0xE0))
  {
    cBitstream bs(buf_ptr, 4 * 8);
    bs.skipBits(11); // syncword

    int audioVersion = bs.readBits(2);
    if (audioVersion == 1)
      return 0;
    int mpeg2 = !(audioVersion & 1);
    int mpeg25 = !(audioVersion & 3);

    int layer = bs.readBits(2);
    if (layer == 0)
      return 0;
    layer = 4 - layer;

    bs.skipBits(1); // protetion bit
    int bitrate_index = bs.readBits(4);
    if (bitrate_index == 15 || bitrate_index == 0)
      return 0;
    m_BitRate  = BitrateTable[mpeg2][layer - 1][bitrate_index] * 1000;

    int sample_rate_index = bs.readBits(2);
    if (sample_rate_index == 3)
      return 0;
    m_SampleRate = FrequencyTable[sample_rate_index] >> (mpeg2 + mpeg25);

    int padding = bs.readBits1();
    bs.skipBits(1); // private bit
    int channel_mode = bs.readBits(2);

    if (channel_mode == 11)
      m_Channels = 1;
    else
      m_Channels = 2;

    if (layer == 1)
      m_FrameSize = (12 * m_BitRate / m_SampleRate + padding) * 4;
    else
      m_FrameSize = 144 * m_BitRate / m_SampleRate + padding;

    m_FoundFrame = true;
    m_DTS = m_curPTS;
    m_PTS = m_curPTS;
    m_curPTS += 90000 * 1152 / m_SampleRate;
    return -1;
  }
  return 0;
}
