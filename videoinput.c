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
 *
 */

#include "config.h"
#include "videoinput.h"
#include "videobuffer.h"
#include "vnsi.h"

#include <vdr/remux.h>
#include <vdr/device.h>
#include <vdr/receiver.h>
#include <vdr/ci.h>
#include <vdr/config.h>
#include <libsi/section.h>
#include <libsi/descriptor.h>
#include <memory>
#include <vector>
#include <algorithm>

// --- cLiveReceiver -------------------------------------------------

class cLiveReceiver: public cReceiver
{
public:
  cLiveReceiver(cVideoInput *VideoInput, const cChannel *Channel, int Priority);
  virtual ~cLiveReceiver();

protected:
  virtual void Activate(bool On);
#if VDRVERSNUM >= 20301
  virtual void Receive(const uchar *Data, int Length);
#else
  virtual void Receive(uchar *Data, int Length);
#endif

  cVideoInput *m_VideoInput;
};

cLiveReceiver::cLiveReceiver(cVideoInput *VideoInput, const cChannel *Channel, int Priority)
 : cReceiver(Channel, Priority)
 , m_VideoInput(VideoInput)
{
  SetPids(NULL);
}

cLiveReceiver::~cLiveReceiver()
{

}

//void cLiveReceiver
#if VDRVERSNUM >= 20301
void cLiveReceiver::Receive(const uchar *Data, int Length)
#else
void cLiveReceiver::Receive(uchar *Data, int Length)
#endif
{
  m_VideoInput->Receive(Data, Length);
}

void cLiveReceiver::Activate(bool On)
{
  INFOLOG("activate live receiver: %d, pmt change: %d", On, m_VideoInput->m_PmtChange);
  if (!On)
    m_VideoInput->m_Event.Signal();
}

// --- cLivePatFilter ----------------------------------------------------

class cLivePatFilter : public cFilter
{
private:
  int             m_pmtPid;
  int             m_pmtSid;
  int             m_pmtVersion;
  const cChannel *m_Channel;
  cVideoInput    *m_VideoInput;

  virtual void Process(u_short Pid, u_char Tid, const u_char *Data, int Length);

public:
  cLivePatFilter(cVideoInput *VideoInput, const cChannel *Channel);
};

cLivePatFilter::cLivePatFilter(cVideoInput *VideoInput, const cChannel *Channel)
{
  DEBUGLOG("cStreamdevPatFilter(\"%s\")", Channel->Name());
  m_Channel     = Channel;
  m_VideoInput  = VideoInput;
  m_pmtPid      = 0;
  m_pmtSid      = 0;
  m_pmtVersion  = -1;
  Set(0x00, 0x00);  // PAT
}

void cLivePatFilter::Process(u_short Pid, u_char Tid, const u_char *Data, int Length)
{
#if VDRVERSNUM < 20104
  if (Pid == 0x00)
  {
    if (Tid == 0x00)
    {
      SI::PAT pat(Data, false);
      if (!pat.CheckCRCAndParse())
        return;
      SI::PAT::Association assoc;
      for (SI::Loop::Iterator it; pat.associationLoop.getNext(assoc, it); )
      {
        if (!assoc.isNITPid())
        {
#if VDRVERSNUM >= 20301
          LOCK_CHANNELS_READ;
          const cChannel *Channel =  Channels->GetByServiceID(Source(), Transponder(), assoc.getServiceId());
#else
          const cChannel *Channel =  Channels.GetByServiceID(Source(), Transponder(), assoc.getServiceId());
#endif
          if (Channel && (Channel == m_Channel))
          {
            int prevPmtPid = m_pmtPid;
            if (0 != (m_pmtPid = assoc.getPid()))
            {
              if (m_pmtPid != prevPmtPid)
              {
                m_pmtSid = assoc.getServiceId();
                Add(m_pmtPid, 0x02);
                m_pmtVersion = -1;
                break;
              }
              return;
            }
          }
        }
      }
    }
  }
  else if (Pid == m_pmtPid && Tid == SI::TableIdPMT && Source() && Transponder())
  {
    SI::PMT pmt(Data, false);
    if (!pmt.CheckCRCAndParse())
      return;
    if (pmt.getServiceId() != m_pmtSid)
      return; // skip broken PMT records
    if (m_pmtVersion != -1)
    {
      if (m_pmtVersion != pmt.getVersionNumber())
      {
        cFilter::Del(m_pmtPid, 0x02);
        m_pmtPid = 0; // this triggers PAT scan
      }
      return;
    }
    m_pmtVersion = pmt.getVersionNumber();

#if VDRVERSNUM >= 20301
    LOCK_CHANNELS_READ;
    const cChannel *Channel = Channels->GetByServiceID(Source(), Transponder(), pmt.getServiceId());
#else
    cChannel *Channel = Channels.GetByServiceID(Source(), Transponder(), pmt.getServiceId());
#endif
    if (Channel) {
       // Scan the stream-specific loop:
       SI::PMT::Stream stream;
       int Vpid = 0;
       int Ppid = 0;
       int Vtype = 0;
       int Apids[MAXAPIDS + 1] = { 0 }; // these lists are zero-terminated
       int Atypes[MAXAPIDS + 1] = { 0 };
       int Dpids[MAXDPIDS + 1] = { 0 };
       int Dtypes[MAXDPIDS + 1] = { 0 };
       int Spids[MAXSPIDS + 1] = { 0 };
       uchar SubtitlingTypes[MAXSPIDS + 1] = { 0 };
       uint16_t CompositionPageIds[MAXSPIDS + 1] = { 0 };
       uint16_t AncillaryPageIds[MAXSPIDS + 1] = { 0 };
       char ALangs[MAXAPIDS][MAXLANGCODE2] = { "" };
       char DLangs[MAXDPIDS][MAXLANGCODE2] = { "" };
       char SLangs[MAXSPIDS][MAXLANGCODE2] = { "" };
       int Tpid = 0;
       int NumApids = 0;
       int NumDpids = 0;
       int NumSpids = 0;
       for (SI::Loop::Iterator it; pmt.streamLoop.getNext(stream, it); ) {
           bool ProcessCaDescriptors = false;
           int esPid = stream.getPid();
           switch (stream.getStreamType()) {
             case 1: // STREAMTYPE_11172_VIDEO
             case 2: // STREAMTYPE_13818_VIDEO
             case 0x1B: // MPEG4
                     Vpid = esPid;
                     Ppid = pmt.getPCRPid();
                     Vtype = stream.getStreamType();
                     ProcessCaDescriptors = true;
                     break;
             case 3: // STREAMTYPE_11172_AUDIO
             case 4: // STREAMTYPE_13818_AUDIO
             case 0x0F: // ISO/IEC 13818-7 Audio with ADTS transport syntax
             case 0x11: // ISO/IEC 14496-3 Audio with LATM transport syntax
                     {
                     if (NumApids < MAXAPIDS) {
                        Apids[NumApids] = esPid;
                        Atypes[NumApids] = stream.getStreamType();
                        SI::Descriptor *d;
                        for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                            switch (d->getDescriptorTag()) {
                              case SI::ISO639LanguageDescriptorTag: {
                                   SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                   SI::ISO639LanguageDescriptor::Language l;
                                   char *s = ALangs[NumApids];
                                   int n = 0;
                                   for (SI::Loop::Iterator it; ld->languageLoop.getNext(l, it); ) {
                                       if (*ld->languageCode != '-') { // some use "---" to indicate "none"
                                          if (n > 0)
                                             *s++ = '+';
                                          strn0cpy(s, I18nNormalizeLanguageCode(l.languageCode), MAXLANGCODE1);
                                          s += strlen(s);
                                          if (n++ > 1)
                                             break;
                                          }
                                       }
                                   }
                                   break;
                              default: ;
                              }
                            delete d;
                            }
                        NumApids++;
                        }
                     ProcessCaDescriptors = true;
                     }
                     break;
             case 5: // STREAMTYPE_13818_PRIVATE
             case 6: // STREAMTYPE_13818_PES_PRIVATE
             //XXX case 8: // STREAMTYPE_13818_DSMCC
                     {
                     int dpid = 0;
                     int dtype = 0;
                     char lang[MAXLANGCODE1] = { 0 };
                     SI::Descriptor *d;
                     for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                         switch (d->getDescriptorTag()) {
                           case SI::AC3DescriptorTag:
                           case SI::EnhancedAC3DescriptorTag:
                                dpid = esPid;
                                dtype = d->getDescriptorTag();
                                ProcessCaDescriptors = true;
                                break;
                           case SI::SubtitlingDescriptorTag:
                                if (NumSpids < MAXSPIDS) {
                                   Spids[NumSpids] = esPid;
                                   SI::SubtitlingDescriptor *sd = (SI::SubtitlingDescriptor *)d;
                                   SI::SubtitlingDescriptor::Subtitling sub;
                                   char *s = SLangs[NumSpids];
                                   int n = 0;
                                   for (SI::Loop::Iterator it; sd->subtitlingLoop.getNext(sub, it); ) {
                                       if (sub.languageCode[0]) {
                                          SubtitlingTypes[NumSpids] = sub.getSubtitlingType();
                                          CompositionPageIds[NumSpids] = sub.getCompositionPageId();
                                          AncillaryPageIds[NumSpids] = sub.getAncillaryPageId();
                                          if (n > 0)
                                             *s++ = '+';
                                          strn0cpy(s, I18nNormalizeLanguageCode(sub.languageCode), MAXLANGCODE1);
                                          s += strlen(s);
                                          if (n++ > 1)
                                             break;
                                          }
                                       }
                                   NumSpids++;
                                   }
                                break;
                           case SI::TeletextDescriptorTag:
                                Tpid = esPid;
                                break;
                           case SI::ISO639LanguageDescriptorTag: {
                                SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                strn0cpy(lang, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
                                }
                                break;
                           default: ;
                           }
                         delete d;
                         }
                     if (dpid) {
                        if (NumDpids < MAXDPIDS) {
                           Dpids[NumDpids] = dpid;
                           Dtypes[NumDpids] = dtype;
                           strn0cpy(DLangs[NumDpids], lang, MAXLANGCODE1);
                           NumDpids++;
                           }
                        }
                     }
                     break;
             case 0x80: // STREAMTYPE_USER_PRIVATE
#if APIVERSNUM >= 10728
                     if (Setup.StandardCompliance == STANDARD_ANSISCTE)
#endif
                     { // DigiCipher II VIDEO (ANSI/SCTE 57)
                        Vpid = esPid;
                        Ppid = pmt.getPCRPid();
                        Vtype = 0x02; // compression based upon MPEG-2
                        ProcessCaDescriptors = true;
                        break;
                        }
                     // fall through
             case 0x81: // STREAMTYPE_USER_PRIVATE
#if APIVERSNUM >= 10728
                     if (Setup.StandardCompliance == STANDARD_ANSISCTE)
#endif
                     { // ATSC A/53 AUDIO (ANSI/SCTE 57)
                        char lang[MAXLANGCODE1] = { 0 };
                        SI::Descriptor *d;
                        for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                            switch (d->getDescriptorTag()) {
                              case SI::ISO639LanguageDescriptorTag: {
                                   SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                   strn0cpy(lang, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
                                   }
                                   break;
                              default: ;
                              }
                           delete d;
                           }
                        if (NumDpids < MAXDPIDS) {
                           Dpids[NumDpids] = esPid;
                           Dtypes[NumDpids] = SI::AC3DescriptorTag;
                           strn0cpy(DLangs[NumDpids], lang, MAXLANGCODE1);
                           NumDpids++;
                           }
                        ProcessCaDescriptors = true;
                        break;
                        }
                     // fall through
             case 0x82: // STREAMTYPE_USER_PRIVATE
#if APIVERSNUM >= 10728
                     if (Setup.StandardCompliance == STANDARD_ANSISCTE)
#endif
                     { // STANDARD SUBTITLE (ANSI/SCTE 27)
                        //TODO
                        break;
                        }
                     // fall through
             case 0x83 ... 0xFF: // STREAMTYPE_USER_PRIVATE
                     {
                     char lang[MAXLANGCODE1] = { 0 };
                     bool IsAc3 = false;
                     SI::Descriptor *d;
                     for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                         switch (d->getDescriptorTag()) {
                           case SI::RegistrationDescriptorTag: {
                                SI::RegistrationDescriptor *rd = (SI::RegistrationDescriptor *)d;
                                // http://www.smpte-ra.org/mpegreg/mpegreg.html
                                switch (rd->getFormatIdentifier()) {
                                  case 0x41432D33: // 'AC-3'
                                       IsAc3 = true;
                                       break;
                                  default:
                                       //printf("Format identifier: 0x%08X (pid: %d)\n", rd->getFormatIdentifier(), esPid);
                                       break;
                                  }
                                }
                                break;
                           case SI::ISO639LanguageDescriptorTag: {
                                SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                strn0cpy(lang, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
                                }
                                break;
                           default: ;
                           }
                        delete d;
                        }
                     if (IsAc3) {
                        if (NumDpids < MAXDPIDS) {
                           Dpids[NumDpids] = esPid;
                           Dtypes[NumDpids] = SI::AC3DescriptorTag;
                           strn0cpy(DLangs[NumDpids], lang, MAXLANGCODE1);
                           NumDpids++;
                           }
                        ProcessCaDescriptors = true;
                        }
                     }
                     break;
             default: ;//printf("PID: %5d %5d %2d %3d %3d\n", pmt.getServiceId(), stream.getPid(), stream.getStreamType(), pmt.getVersionNumber(), Channel->Number());
             }
           }
       DEBUGLOG("Pat/Pmt Filter received pmt change");
       cChannel *pmtChannel = m_VideoInput->PmtChannel();
       pmtChannel->Modification();
       pmtChannel->SetPids(Vpid, Ppid, Vtype, Apids, Atypes, ALangs, Dpids, Dtypes, DLangs, Spids, SLangs, Tpid);
       pmtChannel->SetSubtitlingDescriptors(SubtitlingTypes, CompositionPageIds, AncillaryPageIds);
       if (pmtChannel->Modification(CHANNELMOD_PIDS))
         m_VideoInput->RequestRetune();
    }
  }
#endif
}

// --- cDummyReceiver ----------------------------------------------------

// This dummy receiver is used to detect if a recording/streaming task
// with a higher priority has acquired the device and detached *all*
// receivers from it.
class cDummyReceiver : public cReceiver
{
public:
  // Return a new or existing dummy receiver attached to the device.
  static std::shared_ptr<cDummyReceiver> Create(cDevice *device);
  virtual ~cDummyReceiver() {Detach();}
protected:
#if VDRVERSNUM >= 20301
  virtual void Receive(const uchar *Data, int Length) {}
#else
  virtual void Receive(uchar *Data, int Length) {}
#endif
  virtual void Activate(bool On);
private:
  static std::vector<std::weak_ptr<cDummyReceiver>> s_Pool;
  static cMutex s_PoolMutex;
  cDummyReceiver() : cReceiver(NULL, MINPRIORITY) {}
};

std::vector<std::weak_ptr<cDummyReceiver>> cDummyReceiver::s_Pool;
cMutex cDummyReceiver::s_PoolMutex;

void cDummyReceiver::Activate(bool On)
{
  INFOLOG("Dummy receiver (%p) %s", this, (On)? "activated" : "deactivated");
}

std::shared_ptr<cDummyReceiver> cDummyReceiver::Create(cDevice *device)
{
  if (!device)
    return nullptr;

  cMutexLock MutexLock(&s_PoolMutex);
  // cleanup
  s_Pool.erase(std::remove_if(s_Pool.begin(), s_Pool.end(),
        [](const std::weak_ptr<cDummyReceiver> &p) -> bool
        {return !p.lock();}), s_Pool.end());

  // find an active receiver for the device
  for (auto p : s_Pool)
  {
    auto recv = p.lock();
    if (recv->Device() == device)
      return recv;
  }
  auto recv = std::shared_ptr<cDummyReceiver>(new cDummyReceiver);
  if (device->AttachReceiver(recv.get()))
  {
    s_Pool.push_back(recv);
    return recv;
  }
  return nullptr;
}

// ----------------------------------------------------------------------------

cVideoInput::cVideoInput(cCondWait &event)
  : m_Event(event)
{
  m_Device = NULL;
  m_camSlot = nullptr;
  m_PatFilter = NULL;
  m_Receiver = NULL;;
  m_Channel = NULL;
  m_VideoBuffer = NULL;
  m_Priority = 0;
  m_PmtChange = false;
  m_RetuneRequested = false;
}

cVideoInput::~cVideoInput()
{
  Close();
}

bool cVideoInput::Open(const cChannel *channel, int priority, cVideoBuffer *videoBuffer)
{
  m_VideoBuffer = videoBuffer;
  m_Channel = channel;
  m_Priority = priority;
  m_RetuneRequested = false;
  m_Device = cDevice::GetDevice(m_Channel, m_Priority, false);
  m_camSlot = nullptr;

  if (m_Device != NULL)
  {
    INFOLOG("Successfully found following device: %p (%d) for receiving, priority=%d", m_Device, m_Device ? m_Device->CardIndex() + 1 : 0, m_Priority);

    if (m_Device->SwitchChannel(m_Channel, false))
    {

      m_Device->SetCurrentChannel(m_Channel);

#if VDRVERSNUM < 20104
      m_PatFilter = new cLivePatFilter(this, m_Channel);
      m_Device->AttachFilter(m_PatFilter);
#endif

      m_PmtChannel = *m_Channel;
      m_PmtChange = true;

      m_Receiver = new cLiveReceiver(this, m_Channel, m_Priority);
      m_Receiver->SetPids(NULL);
      m_Receiver->SetPids(&m_PmtChannel);
      m_Receiver->AddPid(m_PmtChannel.Tpid());

      m_DummyReceiver = cDummyReceiver::Create(m_Device);
      if (!m_DummyReceiver)
        return false;

      m_camSlot = m_Device->CamSlot();

#if VDRVERSNUM >= 20107
      if (DisableScrambleTimeout && m_camSlot)
      {
        m_Receiver->SetPriority(MINPRIORITY);
        if (!m_Device->AttachReceiver(m_Receiver))
          return false;
        if (m_camSlot)
          m_camSlot->StartDecrypting();
        m_Receiver->SetPriority(m_Priority);
      }
      else
#endif
      {
        if (!m_Device->AttachReceiver(m_Receiver))
          return false;
      }
      m_VideoBuffer->AttachInput(true);
      return true;
    }
  }
  return false;
}

void cVideoInput::Close()
{
  INFOLOG("close video input ...");

  if (m_Device)
  {
    if (DisableCamBlacklist)
    {
      if (m_Receiver && m_camSlot)
      {
        ChannelCamRelations.ClrChecked(m_Receiver->ChannelID(),
                                       m_camSlot->SlotNumber());
      }
    }

    if (m_Receiver)
    {
      DEBUGLOG("Detaching Live Receiver");
      m_Device->Detach(m_Receiver);
    }
    else
    {
      DEBUGLOG("No live receiver present");
    }

    if (m_PatFilter)
    {
      DEBUGLOG("Detaching Live Filter");
      m_Device->Detach(m_PatFilter);
    }
    else
    {
      DEBUGLOG("No live filter present");
    }

    m_DummyReceiver.reset();

    if (m_Receiver)
    {
      DEBUGLOG("Deleting Live Receiver");
      DELETENULL(m_Receiver);
    }

    if (m_PatFilter)
    {
      DEBUGLOG("Deleting Live Filter");
      DELETENULL(m_PatFilter);
    }
  }
  m_Channel = NULL;
  m_Device = NULL;
  if (m_VideoBuffer)
  {
    m_VideoBuffer->AttachInput(false);
    m_VideoBuffer = NULL;
  }
}

bool cVideoInput::IsOpen()
{
  if (m_Channel)
    return true;
  else
    return false;
}

cChannel *cVideoInput::PmtChannel()
{
  return &m_PmtChannel;
}

inline void cVideoInput::Receive(const uchar *data, int length)
{
  if (m_PmtChange)
  {
     // generate pat/pmt so we can configure parsers later
     cPatPmtGenerator patPmtGenerator(&m_PmtChannel);
     m_VideoBuffer->Put(patPmtGenerator.GetPat(), TS_SIZE);
     int Index = 0;
     while (uchar *pmt = patPmtGenerator.GetPmt(Index))
       m_VideoBuffer->Put(pmt, TS_SIZE);
     m_PmtChange = false;
  }
  m_VideoBuffer->Put(data, length);
}

void cVideoInput::RequestRetune()
{
  m_RetuneRequested = true;
  m_Event.Signal();
}

cVideoInput::eReceivingStatus cVideoInput::ReceivingStatus()
{
  if (!m_Device || !m_Receiver || !m_DummyReceiver)
    return RETUNE;
  if (m_RetuneRequested || !m_Receiver->IsAttached())
  {
    (void)m_Device->Receiving();  // wait for the receivers mutex
    if (!m_DummyReceiver->IsAttached())  // DetachAllReceivers() was called
      return CLOSE;
    else if (!m_PmtChange)
      return RETUNE;
  }
  return NORMAL;
}
