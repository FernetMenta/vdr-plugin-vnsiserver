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

// Warning: This is an unfinished port from H.264 to HEVC in alpha state
// Tested with German DVB-T2 HD channels

#include "parser_hevc.h"
#include "bitstream.h"
#include "config.h"

#include <stdlib.h>
#include <assert.h>


cParserHEVC::cParserHEVC(int pID, cTSStream *stream, sPtsWrap *ptsWrap, bool observePtsWraps)
 : cParser(pID, stream, ptsWrap, observePtsWraps)
{
  m_Height            = 0;
  m_Width             = 0;
  m_FpsScale          = 0;
  m_PixelAspect.den   = 1;
  m_PixelAspect.num   = 0;
  memset(&m_streamData, 0, sizeof(m_streamData));
  m_PesBufferInitialSize = 240000;

  m_IsVideo = true;
  Reset();
}

cParserHEVC::~cParserHEVC()
{
}

void cParserHEVC::Parse(sStreamPacket *pkt, sStreamPacket *pkt_side_data)
{
  if (m_PesBufferPtr < 10) // 2*startcode + header + trail bits
    return;

  int p = m_PesParserPtr;
  uint32_t startcode = m_StartCode;
  bool frameComplete = false;

  while (m_PesBufferPtr - p)
  {
    startcode = startcode << 8 | m_PesBuffer[p++];
    if ((startcode & 0x00ffffff) == 0x00000001)
    {
      if (m_LastStartPos != -1)
         Parse_HEVC(m_LastStartPos, p-m_LastStartPos, &frameComplete);
      m_LastStartPos = p;
      if (frameComplete)
        break;
    }
  }
  m_PesParserPtr = p;
  m_StartCode = startcode;

  if (frameComplete)
  {
    if (!m_NeedSPS && m_FrameValid)
    {
      double PAR = (double)m_PixelAspect.num/(double)m_PixelAspect.den;
      double DAR = (PAR * m_Width) / m_Height;
      DEBUGLOG("HEVC SPS: PAR %i:%i", m_PixelAspect.num, m_PixelAspect.den);
      DEBUGLOG("HEVC SPS: DAR %.2f", DAR);

      int duration;
      if (m_curDTS != DVD_NOPTS_VALUE && m_prevDTS != DVD_NOPTS_VALUE && m_curDTS > m_prevDTS)
        duration = m_curDTS - m_prevDTS;
      else
        duration = m_Stream->Rescale(20000, 90000, DVD_TIME_BASE);

      if (m_FpsScale == 0)
        m_FpsScale = m_Stream->Rescale(duration, DVD_TIME_BASE, 90000);

      bool streamChange = m_Stream->SetVideoInformation(m_FpsScale, DVD_TIME_BASE, m_Height, m_Width, DAR);

      pkt->id       = m_pID;
      pkt->size     = m_PesNextFramePtr;
      pkt->data     = m_PesBuffer;
      pkt->dts      = m_DTS;
      pkt->pts      = m_PTS;
      pkt->duration = duration;
      pkt->streamChange = streamChange;

    }
    m_StartCode = 0xffffffff;
    m_LastStartPos = -1;
    m_PesParserPtr = 0;
    m_FoundFrame = false;
    m_FrameValid = true;
  }
}

void cParserHEVC::Reset()
{
  cParser::Reset();
  m_StartCode = 0xffffffff;
  m_LastStartPos = -1;
  m_NeedSPS = true;
  m_NeedPPS = true;
  memset(&m_streamData, 0, sizeof(m_streamData));
}


void cParserHEVC::Parse_HEVC(int buf_ptr, unsigned int NumBytesInNalUnit, bool *complete)
{
  uint8_t *buf = m_PesBuffer + buf_ptr;
  uint16_t header;
  HDR_NAL hdr;

  // nal_unit_header
  header = (buf[0] << 8) | buf[1];
  if (header & 0x8000) // ignore forbidden_bit == 1
    return;
  hdr.nal_unit_type   = (header & 0x7e00) >> 9;
  hdr.nuh_layer_id    = (header &  0x1f8) >> 3;
  hdr.nuh_temporal_id = (header &    0x7) - 1;

  switch (hdr.nal_unit_type)
  {
  case NAL_TRAIL_N ... NAL_RASL_R:
  case NAL_BLA_W_LP ... NAL_CRA_NUT:
  {
    if (m_NeedSPS || m_NeedPPS)
    {
      m_FoundFrame = true;
      return;
    }
    hevc_private::VCL_NAL vcl;
    memset(&vcl, 0, sizeof(hevc_private::VCL_NAL));
    Parse_SLH(buf, NumBytesInNalUnit, hdr, vcl);
 
    // check for the beginning of a new access unit
    if (m_FoundFrame && IsFirstVclNal(vcl))
    {
      *complete = true;
      m_PesNextFramePtr = buf_ptr - 3;
      return;
    }

    if (!m_FoundFrame)
    {
      if (buf_ptr - 3 >= m_PesTimePos)
      {
        m_DTS = m_curDTS;
        m_PTS = m_curPTS;
      }
      else
      {
        m_DTS = m_prevDTS;
        m_PTS = m_prevPTS;
      }
    }

    m_streamData.vcl_nal = vcl;
    m_FoundFrame = true;
    break;
  }

  case NAL_PFX_SEI_NUT:
    if (m_FoundFrame)
    {
      *complete = true;
      m_PesNextFramePtr = buf_ptr - 3;
    }
    break;

  case NAL_VPS_NUT:
     break;

  case NAL_SPS_NUT:
  {
    if (m_FoundFrame)
    {
      *complete = true;
      m_PesNextFramePtr = buf_ptr - 3;
      return;
    }
    Parse_SPS(buf, NumBytesInNalUnit, hdr);
    m_NeedSPS = false;
    break;
  }

  case NAL_PPS_NUT:
  {
    if (m_FoundFrame)
    {
      *complete = true;
      m_PesNextFramePtr = buf_ptr - 3;
      return;
    }
    Parse_PPS(buf, NumBytesInNalUnit);
    m_NeedPPS = false;
    break;
  }

  case NAL_AUD_NUT:
    if (m_FoundFrame && (m_prevPTS != DVD_NOPTS_VALUE))
    {
      *complete = true;
      m_PesNextFramePtr = buf_ptr - 3;
    }
    break;

  case NAL_EOS_NUT:
    if (m_FoundFrame)
    {
      *complete = true;
      m_PesNextFramePtr = buf_ptr + 2;
    }
    break;

  case NAL_FD_NUT:
  case NAL_SFX_SEI_NUT:
     break;

  default:
    INFOLOG("HEVC fixme: nal unknown %i", hdr.nal_unit_type);
    break;
  }
}

void cParserHEVC::Parse_PPS(uint8_t *buf, int len)
{
  cBitstream bs(buf, len*8, true);

  int pps_id = bs.readGolombUE();
  int sps_id = bs.readGolombUE();
  m_streamData.pps[pps_id].sps = sps_id;
  m_streamData.pps[pps_id].dependent_slice_segments_enabled_flag = bs.readBits(1);
}

void cParserHEVC::Parse_SLH(uint8_t *buf, int len, HDR_NAL hdr, hevc_private::VCL_NAL &vcl)
{
  cBitstream bs(buf, len*8, true);

  vcl.nal_unit_type = hdr.nal_unit_type;

  vcl.first_slice_segment_in_pic_flag = bs.readBits(1);

  if ((hdr.nal_unit_type >= NAL_BLA_W_LP) && (hdr.nal_unit_type <= NAL_RSV_IRAP_VCL23))
    bs.skipBits(1); // no_output_of_prior_pics_flag

  vcl.pic_parameter_set_id = bs.readGolombUE();
}

// 7.3.2.2.1 General sequence parameter set RBSP syntax
void cParserHEVC::Parse_SPS(uint8_t *buf, int len, HDR_NAL hdr)
{
  cBitstream bs(buf, len*8, true);
  unsigned int i;
  int sub_layer_profile_present_flag[8], sub_layer_level_present_flag[8];

  bs.skipBits(4); // sps_video_parameter_set_id

  unsigned int sps_max_sub_layers_minus1 = bs.readBits(3);
  bs.skipBits(1); // sps_temporal_id_nesting_flag

  // skip over profile_tier_level
  bs.skipBits(8 + 32 + 4 + 43 + 1 +8);
  for (i=0; i<sps_max_sub_layers_minus1; i++)
  {
    sub_layer_profile_present_flag[i] = bs.readBits(1);
    sub_layer_level_present_flag[i] = bs.readBits(1);
  }
  if (sps_max_sub_layers_minus1 > 0)
  {
    for (i=sps_max_sub_layers_minus1; i<8; i++)
      bs.skipBits(2);
  }
  for (i=0; i<sps_max_sub_layers_minus1; i++)
  {
    if (sub_layer_profile_present_flag[i])
      bs.skipBits(8 + 32 + 4 + 43 + 1);
    if (sub_layer_level_present_flag[i])
      bs.skipBits(8);
  }
  // end skip over profile_tier_level

  bs.readGolombUE(); // sps_seq_parameter_set_id
  unsigned int chroma_format_idc = bs.readGolombUE();
 
  if (chroma_format_idc == 3)
    bs.skipBits(1); // separate_colour_plane_flag

  m_Width  = bs.readGolombUE();
  m_Height = bs.readGolombUE();
  m_PixelAspect.num = 1;
}

bool cParserHEVC::IsFirstVclNal(hevc_private::VCL_NAL &vcl)
{
  if (m_streamData.vcl_nal.pic_parameter_set_id != vcl.pic_parameter_set_id)
    return true;

  if (vcl.first_slice_segment_in_pic_flag)
    return true;

  return false;
}

