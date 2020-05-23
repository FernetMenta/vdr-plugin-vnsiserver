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

#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>

// work-around for VDR's tools.h
#if VDRVERSNUM < 20400
#define __STL_CONFIG_H 1
#else
#define DISABLE_TEMPLATES_COLLIDING_WITH_STL 1
#endif
#include "streamer.h"

#include "config.h"
#include "cxsocket.h"
#include "vnsicommand.h"
#include "responsepacket.h"
#include "vnsi.h"
#include "videobuffer.h"

#include <vdr/channels.h>
#include <vdr/eitscan.h>

// --- cLiveStreamer -------------------------------------------------

cLiveStreamer::cLiveStreamer(int clientID, bool bAllowRDS, int protocol, uint8_t timeshift, uint32_t timeout)
 : cThread("cLiveStreamer stream processor")
 , m_ClientID(clientID)
 , m_scanTimeout(timeout)
 , m_Demuxer(bAllowRDS)
 , m_VideoInput(m_Event)
{
  m_protocolVersion = protocol;
  m_Timeshift = timeshift;
  m_refTime = -1;
  m_refDTS = -2;

  memset(&m_FrontendInfo, 0, sizeof(m_FrontendInfo));

  if(m_scanTimeout == 0)
    m_scanTimeout = VNSIServerConfig.stream_timeout;
}

cLiveStreamer::~cLiveStreamer()
{
  DEBUGLOG("Started to delete live streamer");

  Cancel(5);
  Close();

  DEBUGLOG("Finished to delete live streamer");
}

bool cLiveStreamer::Open(int serial)
{
  Close();

#if APIVERSNUM >= 10725
  m_Device = cDevice::GetDevice(m_Channel, m_Priority, true, true);
#else
  m_Device = cDevice::GetDevice(m_Channel, m_Priority, true);
#endif

  if (!m_Device)
    return false;

  bool recording = false;
  if (VNSIServerConfig.testStreamActive) // test harness
  {
    recording = true;
    m_VideoBuffer = cVideoBuffer::Create(VNSIServerConfig.testStreamFile);
  }
  else if (PlayRecording && serial == -1)
  {
#if VDRVERSNUM >= 20301
    LOCK_TIMERS_READ;
    for (const cTimer *timer = Timers->First(); timer; timer = Timers->Next(timer))
#else
    for (cTimer *timer = Timers.First(); timer; timer = Timers.Next(timer))
#endif
    {
      if (timer &&
          timer->Recording() &&
          timer->Channel() == m_Channel)
      {
#if VDRVERSNUM >= 20301
        LOCK_RECORDINGS_READ;
        cTimer t(*timer);
        cRecording matchRec(&t, t.Event());
        const cRecording *rec;
        {
          rec = Recordings->GetByName(matchRec.FileName());
          if (!rec)
          {
            return false;
          }
        }
#else
        Recordings.Load();
        cRecording matchRec(timer, timer->Event());
        cRecording *rec;
        {
          cThreadLock RecordingsLock(&Recordings);
          rec = Recordings.GetByName(matchRec.FileName());
          if (!rec)
          {
            return false;
          }
        }
#endif
        m_VideoBuffer = cVideoBuffer::Create(rec);
        recording = true;
        break;
      }
    }
  }
  if (!recording)
  {
    m_VideoBuffer = cVideoBuffer::Create(m_ClientID, m_Timeshift);
  }

  if (!m_VideoBuffer)
    return false;

  if (!recording)
  {
    if (m_Channel && ((m_Channel->Source() >> 24) == 'V'))
      m_IsMPEGPS = true;

    if (!m_VideoInput.Open(m_Channel, m_Priority, m_VideoBuffer))
    {
      ERRORLOG("Can't switch to channel %i - %s", m_Channel->Number(), m_Channel->Name());
      return false;
    }
  }

  m_Demuxer.Open(*m_Channel, m_VideoBuffer);
  if (serial >= 0)
    m_Demuxer.SetSerial(serial);

  return true;
}

void cLiveStreamer::Close(void)
{
  INFOLOG("LiveStreamer::Close - close");
  m_VideoInput.Close();
  m_Demuxer.Close();
  if (m_VideoBuffer)
  {
    delete m_VideoBuffer;
    m_VideoBuffer = NULL;
  }

  if (m_Frontend >= 0)
  {
    close(m_Frontend);
    m_Frontend = -1;
  }
}

void cLiveStreamer::Action(void)
{
  int ret;
  sStreamPacket pkt_data;
  sStreamPacket pkt_side_data; // Additional data
  memset(&pkt_data, 0, sizeof(sStreamPacket));
  memset(&pkt_side_data, 0, sizeof(sStreamPacket));
  bool requestStreamChangeData = false;
  bool requestStreamChangeSideData = false;
  cTimeMs last_info(1000);
  cTimeMs bufferStatsTimer(1000);
  int openFailCount = 0;

  while (Running())
  {
    cVideoInput::eReceivingStatus retune = cVideoInput::NORMAL;
    if (m_VideoInput.IsOpen())
      retune = m_VideoInput.ReceivingStatus();
    if (retune == cVideoInput::RETUNE)
      // allow timeshift playback when retune == cVideoInput::CLOSE
      ret = -1;
    else
      ret = m_Demuxer.Read(&pkt_data, &pkt_side_data);

    if (ret > 0)
    {
      if (pkt_data.pmtChange)
      {
        requestStreamChangeData = true;
        requestStreamChangeSideData = true;
      }

      // Process normal data if present
      if (pkt_data.data)
      {
        if (pkt_data.streamChange || requestStreamChangeData)
          sendStreamChange();
        requestStreamChangeData = false;
        if (pkt_data.reftime)
        {
          m_refTime = pkt_data.reftime;
          m_refDTS = pkt_data.dts;
          m_curDTS = (pkt_data.dts - m_refDTS) / DVD_TIME_BASE + m_refTime;
          if (m_protocolVersion >= 11)
          {
            sendStreamTimes();
            bufferStatsTimer.Set(1000);
          }
          else
            sendRefTime(pkt_data);
          pkt_data.reftime = 0;
        }
        m_curDTS = (pkt_data.dts - m_refDTS) / DVD_TIME_BASE + m_refTime;
        if (bufferStatsTimer.TimedOut())
        {
          sendStreamTimes();
          bufferStatsTimer.Set(1000);
        }
        sendStreamPacket(&pkt_data);
      }

      // If some additional data is present inside the stream, it is written there (currently RDS inside MPEG2-Audio)
      if (pkt_side_data.data)
      {
        if (pkt_side_data.streamChange || requestStreamChangeSideData)
          sendStreamChange();
        requestStreamChangeSideData = false;

        sendStreamPacket(&pkt_side_data);
        pkt_side_data.data = NULL;
      }

      // send signal info every 10 sec.
      if (last_info.TimedOut())
      {
        last_info.Set(10000);
        sendSignalInfo();

        // prevent EPG scan (activity timeout is 60s)
        // EPG scan can cause artifacts on dual tuner cards
        if (AvoidEPGScan)
        {
          EITScanner.Activity();
        }
      }

      if (m_protocolVersion < 11)
      {
        // send buffer stats
        if (bufferStatsTimer.TimedOut())
        {
          sendBufferStatus();
          bufferStatsTimer.Set(1000);
        }
      }
    }
    else if (ret == -1)
    {
      // no data
      if (retune == cVideoInput::CLOSE)
      {
        m_Socket->Shutdown();
        break;
      }
      if (m_Demuxer.GetError() & ERROR_CAM_ERROR)
      {
        INFOLOG("CAM error, try reset");
        cCamSlot *cs = m_Device->CamSlot();
        if (cs)
          cs->StopDecrypting();
        retune = cVideoInput::RETUNE;
      }
      if (retune == cVideoInput::RETUNE)
      {
        INFOLOG("re-tuning...");
        m_VideoInput.Close();
        if (!m_VideoInput.Open(m_Channel, m_Priority, m_VideoBuffer))
        {
          if (++openFailCount == 3)
          {
            openFailCount = 0;
            cCondWait::SleepMs(2000);
          }
          else
            cCondWait::SleepMs(100);
        }
        else
          openFailCount = 0;
      }
      else
        m_Event.Wait(10);

      if(m_last_tick.Elapsed() >= (uint64_t)(m_scanTimeout*1000))
      {
        sendStreamStatus();
        m_last_tick.Set(0);
        m_SignalLost = true;
      }
    }
    else if (ret == -2)
    {
      if (!Open(m_Demuxer.GetSerial()))
      {
        m_Socket->Shutdown();
        break;
      }
    }
  }
  INFOLOG("exit streamer thread");
}

bool cLiveStreamer::StreamChannel(const cChannel *channel, int priority, cxSocket *Socket, cResponsePacket *resp)
{
  if (channel == NULL)
  {
    ERRORLOG("Starting streaming of channel without valid channel");
    return false;
  }

  m_Channel   = channel;
  m_Priority  = priority;
  m_Socket    = Socket;

  if (m_Priority < 0)
    m_Priority = 0;

  if (!Open())
    return false;

  // Send the OK response here, that it is before the Stream end message
  resp->add_U32(VNSI_RET_OK);
  resp->finalise();
  m_Socket->write(resp->getPtr(), resp->getLen());

  Activate(true);

  INFOLOG("Successfully switched to channel %i - %s", m_Channel->Number(), m_Channel->Name());
  return true;
}

inline void cLiveStreamer::Activate(bool On)
{
  if (On)
  {
    DEBUGLOG("VDR active, sending stream start message");
    Start();
  }
  else
  {
    DEBUGLOG("VDR inactive, sending stream end message");
    Cancel(5);
  }
}

void cLiveStreamer::sendStreamPacket(sStreamPacket *pkt)
{
  if (pkt == NULL)
    return;

  if (pkt->size == 0)
    return;

  m_streamHeader.initStream(VNSI_STREAM_MUXPKT, pkt->id, pkt->duration, pkt->pts, pkt->dts, pkt->serial);
  m_streamHeader.setLen(m_streamHeader.getStreamHeaderLength() + pkt->size);
  m_streamHeader.finaliseStream();

  m_Socket->LockWrite();
  m_Socket->write(m_streamHeader.getPtr(), m_streamHeader.getStreamHeaderLength(), -1, true);
  m_Socket->write(pkt->data, pkt->size);
  m_Socket->UnlockWrite();

  m_last_tick.Set(0);
  m_SignalLost = false;
}

void cLiveStreamer::sendStreamChange()
{
  cResponsePacket resp;
  resp.initStream(VNSI_STREAM_CHANGE, 0, 0, 0, 0, 0);

  uint32_t FpsScale, FpsRate, Height, Width;
  double Aspect;
  uint32_t Channels, SampleRate, BitRate, BitsPerSample, BlockAlign;
  for (cTSStream* stream = m_Demuxer.GetFirstStream(); stream; stream = m_Demuxer.GetNextStream())
  {
    resp.add_U32(stream->GetPID());
    if (stream->Type() == stMPEG2AUDIO)
    {
      stream->GetAudioInformation(Channels, SampleRate, BitRate, BitsPerSample, BlockAlign);
      resp.add_String("MPEG2AUDIO");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(Channels);
      resp.add_U32(SampleRate);
      resp.add_U32(BlockAlign);
      resp.add_U32(BitRate);
      resp.add_U32(BitsPerSample);

      for (const auto &i : stream->GetSideDataTypes())
      {
        resp.add_U32(i.first);
        if (i.second == scRDS)
        {
          resp.add_String("RDS");
          resp.add_String(stream->GetLanguage());
          resp.add_U32(stream->GetPID());
        }
      }
    }
    else if (stream->Type() == stMPEG2VIDEO)
    {
      stream->GetVideoInformation(FpsScale, FpsRate, Height, Width, Aspect);
      resp.add_String("MPEG2VIDEO");
      resp.add_U32(FpsScale);
      resp.add_U32(FpsRate);
      resp.add_U32(Height);
      resp.add_U32(Width);
      resp.add_double(Aspect);
    }
    else if (stream->Type() == stAC3)
    {
      stream->GetAudioInformation(Channels, SampleRate, BitRate, BitsPerSample, BlockAlign);
      resp.add_String("AC3");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(Channels);
      resp.add_U32(SampleRate);
      resp.add_U32(BlockAlign);
      resp.add_U32(BitRate);
      resp.add_U32(BitsPerSample);
    }
    else if (stream->Type() == stH264)
    {
      stream->GetVideoInformation(FpsScale, FpsRate, Height, Width, Aspect);
      resp.add_String("H264");
      resp.add_U32(FpsScale);
      resp.add_U32(FpsRate);
      resp.add_U32(Height);
      resp.add_U32(Width);
      resp.add_double(Aspect);
    }
    else if (stream->Type() == stHEVC)
    {
      stream->GetVideoInformation(FpsScale, FpsRate, Height, Width, Aspect);
      resp.add_String("HEVC");
      resp.add_U32(FpsScale);
      resp.add_U32(FpsRate);
      resp.add_U32(Height);
      resp.add_U32(Width);
      resp.add_double(Aspect);
    }
    else if (stream->Type() == stDVBSUB)
    {
      resp.add_String("DVBSUB");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(stream->CompositionPageId());
      resp.add_U32(stream->AncillaryPageId());
    }
    else if (stream->Type() == stTELETEXT)
    {
      resp.add_String("TELETEXT");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(stream->CompositionPageId());
      resp.add_U32(stream->AncillaryPageId());
    }
    else if (stream->Type() == stAACADTS)
    {
      stream->GetAudioInformation(Channels, SampleRate, BitRate, BitsPerSample, BlockAlign);
      resp.add_String("AAC");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(Channels);
      resp.add_U32(SampleRate);
      resp.add_U32(BlockAlign);
      resp.add_U32(BitRate);
      resp.add_U32(BitsPerSample);
    }
    else if (stream->Type() == stAACLATM)
    {
      stream->GetAudioInformation(Channels, SampleRate, BitRate, BitsPerSample, BlockAlign);
      resp.add_String("AAC_LATM");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(Channels);
      resp.add_U32(SampleRate);
      resp.add_U32(BlockAlign);
      resp.add_U32(BitRate);
      resp.add_U32(BitsPerSample);
    }
    else if (stream->Type() == stEAC3)
    {
      stream->GetAudioInformation(Channels, SampleRate, BitRate, BitsPerSample, BlockAlign);
      resp.add_String("EAC3");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(Channels);
      resp.add_U32(SampleRate);
      resp.add_U32(BlockAlign);
      resp.add_U32(BitRate);
      resp.add_U32(BitsPerSample);
    }
    else if (stream->Type() == stDTS)
    {
      stream->GetAudioInformation(Channels, SampleRate, BitRate, BitsPerSample, BlockAlign);
      resp.add_String("DTS");
      resp.add_String(stream->GetLanguage());
      resp.add_U32(Channels);
      resp.add_U32(SampleRate);
      resp.add_U32(BlockAlign);
      resp.add_U32(BitRate);
      resp.add_U32(BitsPerSample);
    }
  }

  resp.finaliseStream();
  m_Socket->write(resp.getPtr(), resp.getLen());
}

// taken from vdr 2.3.4+ keeping the comments
static uint16_t dB1000toRelative(__s64 dB1000, int Low, int High)
{
  // Convert the given value, which is in 1/1000 dBm, to a percentage in the
  // range 0..0xffff. Anything below Low is considered 0, and anything above
  // High counts as 0xffff.
  if (dB1000 < Low)
    return 0;
  if (dB1000 > High)
    return 0xffff;
  // return 0xffff - 0xffff * (High - dB1000) / (High - Low); // linear conversion
  // return 0xffff - 0xffff * sqr(dB1000 - High) / sqr(Low - High); // quadratic conversion, see https://www.adriangranados.com/blog/dbm-to-percent-conversion
  dB1000 = 256 * 256 * (dB1000 - High) / (Low - High); // avoids the sqr() function
  dB1000 = (dB1000 * dB1000) / 0x10000;

  if (dB1000 > 0xffff)
    return 0;
  return (uint16_t)(0xffff - dB1000);
}

void cLiveStreamer::sendSignalInfo()
{

  /* If no frontend is found m_Frontend is set to -2, in this case
     return a empty signalinfo package */
  if (m_Frontend == -2)
  {
    cResponsePacket resp;
    resp.initStream(VNSI_STREAM_SIGNALINFO, 0, 0, 0, 0, 0);
    resp.add_String(*cString::sprintf("Unknown"));
    resp.add_String(*cString::sprintf("Unknown"));
    resp.add_U32(0);
    resp.add_U32(0);
    resp.add_U32(0);
    resp.add_U32(0);

    resp.finaliseStream();

    if (m_statusSocket)
      m_statusSocket->write(resp.getPtr(), resp.getLen());
    else
      m_Socket->write(resp.getPtr(), resp.getLen());
    return;
  }

  if (m_Channel && ((m_Channel->Source() >> 24) == 'V'))
  {
    if (m_Frontend < 0)
    {
      for (int i = 0; i < 8; i++)
      {
        m_DeviceString = cString::sprintf("/dev/video%d", i);
        m_Frontend = open(m_DeviceString, O_RDONLY | O_NONBLOCK);
        if (m_Frontend >= 0)
        {
          if (ioctl(m_Frontend, VIDIOC_QUERYCAP, &m_vcap) < 0)
          {
            ERRORLOG("cannot read analog frontend info.");
            close(m_Frontend);
            m_Frontend = -1;
            memset(&m_vcap, 0, sizeof(m_vcap));
            continue;
          }
          break;
        }
      }
      if (m_Frontend < 0)
        m_Frontend = -2;
    }

    if (m_Frontend >= 0)
    {
      cResponsePacket resp;
      resp.initStream(VNSI_STREAM_SIGNALINFO, 0, 0, 0, 0, 0);
      resp.add_String(*cString::sprintf("Analog #%s - %s (%s)", *m_DeviceString, (char *) m_vcap.card, m_vcap.driver));
      resp.add_String("");
      resp.add_U32(0);
      resp.add_U32(0);
      resp.add_U32(0);
      resp.add_U32(0);

      resp.finaliseStream();

      if (m_statusSocket)
        m_statusSocket->write(resp.getPtr(), resp.getLen());
      else
        m_Socket->write(resp.getPtr(), resp.getLen());
    }
  }
  else
  {
    if (m_Frontend < 0)
    {
      m_DeviceString = cString::sprintf(FRONTEND_DEVICE, m_Device->CardIndex(), 0);
      m_Frontend = open(m_DeviceString, O_RDONLY | O_NONBLOCK);
      if (m_Frontend >= 0)
      {
        if (ioctl(m_Frontend, FE_GET_INFO, &m_FrontendInfo) < 0)
        {
          ERRORLOG("cannot read frontend info.");
          close(m_Frontend);
          m_Frontend = -2;
          memset(&m_FrontendInfo, 0, sizeof(m_FrontendInfo));
          return;
        }
      }
    }

    if (m_Frontend >= 0)
    {
      cResponsePacket resp;
      resp.initStream(VNSI_STREAM_SIGNALINFO, 0, 0, 0, 0, 0);

      fe_status_t status;
      uint16_t fe_snr;
      uint16_t fe_signal;
      uint32_t fe_ber;
      uint32_t fe_unc;

      memset(&status, 0, sizeof(status));
      ioctl(m_Frontend, FE_READ_STATUS, &status);

      bool signal_needed(true), snr_needed(true), ber_needed(true), unc_needed(true);
      dtv_property fe_props[4];
      dtv_properties fe_cmd;
      memset(&fe_props, 0, sizeof(fe_props));
      memset(&fe_cmd, 0, sizeof(fe_cmd));

      fe_props[0].cmd = DTV_STAT_SIGNAL_STRENGTH;
      fe_props[1].cmd = DTV_STAT_CNR;
      fe_props[2].cmd = DTV_STAT_PRE_ERROR_BIT_COUNT;
      fe_props[3].cmd = DTV_STAT_ERROR_BLOCK_COUNT;

      fe_cmd.props = fe_props;
      fe_cmd.num = 4;

      if (ioctl(m_Frontend, FE_GET_PROPERTY, &fe_cmd) == 0)
      {
        if (fe_props[0].u.st.len > 0)
        {
          switch (fe_props[0].u.st.stat[0].scale)
          {
            case FE_SCALE_RELATIVE:
              fe_signal = (uint16_t)fe_props[0].u.st.stat[0].uvalue;
              signal_needed = false;
              break;
            case FE_SCALE_DECIBEL:
              fe_signal = dB1000toRelative(fe_props[0].u.st.stat[0].svalue, -95000, -20000);
              signal_needed = false;
              DEBUGLOG("Signal decibel: %d -> %d",
                       (int)fe_props[0].u.st.stat[0].svalue, (int)fe_signal);
              break;
          }
        }
        if (fe_props[1].u.st.len > 0)
        {
          switch (fe_props[1].u.st.stat[0].scale)
          {
            case FE_SCALE_RELATIVE:
              fe_snr = (uint16_t)fe_props[1].u.st.stat[0].uvalue;
              snr_needed = false;
              break;
            case FE_SCALE_DECIBEL:
              fe_snr = dB1000toRelative(fe_props[1].u.st.stat[0].svalue, 5000, 20000);
              snr_needed = false;
              DEBUGLOG("SNR decibel: %d -> %d",
                       (int)fe_props[1].u.st.stat[0].svalue, (int)fe_snr);
              break;
          }
        }
        if (fe_props[2].u.st.len > 0)
        {
          switch (fe_props[2].u.st.stat[0].scale)
          {
            case FE_SCALE_COUNTER:
              fe_ber = (uint32_t)fe_props[2].u.st.stat[0].uvalue;
              ber_needed = false;
              break;
            case FE_SCALE_NOT_AVAILABLE:
              break;
            default:
              DEBUGLOG("BER scale: %d", fe_props[2].u.st.stat[0].scale);
          }
        }
        if (fe_props[3].u.st.len > 0)
        {
          switch (fe_props[3].u.st.stat[0].scale)
          {
            case FE_SCALE_COUNTER:
              fe_unc = (uint32_t)fe_props[3].u.st.stat[0].uvalue;
              unc_needed = false;
              break;
            case FE_SCALE_NOT_AVAILABLE:
              break;
            default:
              DEBUGLOG("UNC scale: %d", fe_props[3].u.st.stat[0].scale);
          }
        }
      }

      if (signal_needed && ioctl(m_Frontend, FE_READ_SIGNAL_STRENGTH, &fe_signal) == -1)
        fe_signal = -2;
      if (snr_needed && ioctl(m_Frontend, FE_READ_SNR, &fe_snr) == -1)
        fe_snr = -2;
      if (ber_needed && ioctl(m_Frontend, FE_READ_BER, &fe_ber) == -1)
        fe_ber = -2;
      if (unc_needed && ioctl(m_Frontend, FE_READ_UNCORRECTED_BLOCKS, &fe_unc) == -1)
        fe_unc = -2;

      switch (m_Channel->Source() & cSource::st_Mask)
      {
        case cSource::stSat:
          resp.add_String(*cString::sprintf("DVB-S%s #%d - %s", (m_FrontendInfo.caps & 0x10000000) ? "2" : "",  m_Device->CardIndex(), m_FrontendInfo.name));
          break;
        case cSource::stCable:
          resp.add_String(*cString::sprintf("DVB-C #%d - %s", m_Device->CardIndex(), m_FrontendInfo.name));
          break;
        case cSource::stTerr:
          resp.add_String(*cString::sprintf("DVB-T #%d - %s", m_Device->CardIndex(), m_FrontendInfo.name));
          break;
        case cSource::stAtsc:
          resp.add_String(*cString::sprintf("ATSC #%d - %s", m_Device->CardIndex(), m_FrontendInfo.name));
          break;
        default:
          resp.add_U8(0);
          break;
      }
      resp.add_String(*cString::sprintf("%s:%s:%s:%s:%s", (status & FE_HAS_LOCK) ? "LOCKED" : "-", (status & FE_HAS_SIGNAL) ? "SIGNAL" : "-", (status & FE_HAS_CARRIER) ? "CARRIER" : "-", (status & FE_HAS_VITERBI) ? "VITERBI" : "-", (status & FE_HAS_SYNC) ? "SYNC" : "-"));
      resp.add_U32(fe_snr);
      resp.add_U32(fe_signal);
      resp.add_U32(fe_ber);
      resp.add_U32(fe_unc);

      resp.finaliseStream();

      if (m_statusSocket)
        m_statusSocket->write(resp.getPtr(), resp.getLen());
      else
        m_Socket->write(resp.getPtr(), resp.getLen());
    }
  }
}

void cLiveStreamer::sendStreamStatus()
{
  cResponsePacket resp;
  resp.initStream(VNSI_STREAM_STATUS, 0, 0, 0, 0, 0);
  uint16_t error = m_Demuxer.GetError();
  if (error & ERROR_PES_SCRAMBLE)
  {
    INFOLOG("Channel: scrambled (PES) %d", error);
    resp.add_String(cString::sprintf("Channel: scrambled (%d)", error));
  }
  else if (error & ERROR_TS_SCRAMBLE)
  {
    INFOLOG("Channel: scrambled (TS) %d", error);
    resp.add_String(cString::sprintf("Channel: scrambled (%d)", error));
  }
  else if (error & ERROR_PES_STARTCODE)
  {
    INFOLOG("Channel: startcode %d", error);
    resp.add_String(cString::sprintf("Channel: encrypted? (%d)", error));
  }
  else if (error & ERROR_DEMUX_NODATA)
  {
    INFOLOG("Channel: no data %d", error);
    resp.add_String(cString::sprintf("Channel: no data"));
  }
  else
  {
    INFOLOG("Channel: unknown error %d", error);
    resp.add_String(cString::sprintf("Channel: unknown error (%d)", error));
  }

  resp.finaliseStream();

  if (m_statusSocket)
    m_statusSocket->write(resp.getPtr(), resp.getLen());
  else
    m_Socket->write(resp.getPtr(), resp.getLen());
}

void cLiveStreamer::sendStreamTimes()
{
  if (m_Channel == NULL)
    return;

  cResponsePacket resp;
  resp.initStream(VNSI_STREAM_TIMES, 0, 0, 0, 0, 0);

  time_t starttime = m_refTime;
  int64_t refDTS = m_refDTS;
  time_t current = m_curDTS;

  {
#if VDRVERSNUM >= 20301
    LOCK_SCHEDULES_READ;
    const cSchedule *schedule = Schedules->GetSchedule(m_Channel);
#else
    cSchedulesLock MutexLock;
    const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
    if (!Schedules)
      return;
    const cSchedule *schedule = Schedules->GetSchedule(m_Channel);
#endif
    if (!schedule)
      return;
    const cEvent *event = schedule->GetEventAround(current);
    if (event)
    {
      starttime = event->StartTime();
      refDTS = (starttime - m_refTime) * DVD_TIME_BASE + m_refDTS;
    }
  }
  uint32_t start, end;
  bool timeshift;
  int64_t mintime = current;
  int64_t maxtime = current;
  m_Demuxer.BufferStatus(timeshift, start, end);
  if (timeshift)
  {
    mintime = (start - starttime) * DVD_TIME_BASE + refDTS;
    maxtime = (end - starttime) * DVD_TIME_BASE + refDTS;
  }

  resp.add_U8(timeshift);
  resp.add_U32(starttime);
  resp.add_U64(refDTS);
  resp.add_U64(mintime);
  resp.add_U64(maxtime);
  resp.finaliseStream();

  if (m_statusSocket)
    m_statusSocket->write(resp.getPtr(), resp.getLen());
  else
    m_Socket->write(resp.getPtr(), resp.getLen());
}

void cLiveStreamer::sendBufferStatus()
{
  cResponsePacket resp;
  resp.initStream(VNSI_STREAM_BUFFERSTATS, 0, 0, 0, 0, 0);
  uint32_t start, end;
  bool timeshift;
  m_Demuxer.BufferStatus(timeshift, start, end);
  resp.add_U8(timeshift);
  resp.add_U32(start);
  resp.add_U32(end);
  resp.finaliseStream();
  m_Socket->write(resp.getPtr(), resp.getLen());
}

void cLiveStreamer::sendRefTime(sStreamPacket &pkt)
{
  cResponsePacket resp;
  resp.initStream(VNSI_STREAM_REFTIME, 0, 0, 0, 0, 0);
  resp.add_U32(pkt.reftime);
  resp.add_U64(pkt.pts);
  resp.finaliseStream();
  m_Socket->write(resp.getPtr(), resp.getLen());
}

bool cLiveStreamer::SeekTime(int64_t time, uint32_t &serial)
{
  bool ret = m_Demuxer.SeekTime(time);
  serial = m_Demuxer.GetSerial();
  return ret;
}

void cLiveStreamer::RetuneChannel(const cChannel *channel)
{
  if (m_Channel != channel || !m_VideoInput.IsOpen())
    return;

  INFOLOG("re-tune to channel %s", m_Channel->Name());
  m_VideoInput.RequestRetune();
}

void cLiveStreamer::AddStatusSocket(int fd)
{
  m_statusSocket.reset(new cxSocket(fd));
}

void cLiveStreamer::SendStatus()
{
  sendStreamTimes();
}
