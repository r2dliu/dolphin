#include "SlippiSavestate.h"
#include <vector>
#include "Common/CommonFuncs.h"
#include "Common/MemoryUtil.h"
#include "Core/HW/AudioInterface.h"
#include "Core/HW/DSP.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/HW.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/VideoInterface.h"

SlippiSavestate::SlippiSavestate()
{
  InitBackupLocs();

  for (auto it = m_backup_locs.begin(); it != m_backup_locs.end(); ++it)
  {
    auto size = it->end_address - it->start_address;
    it->data = static_cast<u8*>(Common::AllocateAlignedMemory(size, 64));
  }

  // u8 *ptr = nullptr;
  // PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);

  // getDolphinState(p);
  // const size_t buffer_size = reinterpret_cast<size_t>(ptr);
  // dolphinSsBackup.resize(buffer_size);
}

SlippiSavestate::~SlippiSavestate()
{
  for (auto it = m_backup_locs.begin(); it != m_backup_locs.end(); ++it)
  {
    Common::FreeAlignedMemory(it->data);
  }
}

bool cmpFn(SlippiSavestate::PreserveBlock pb1, SlippiSavestate::PreserveBlock pb2)
{
  return pb1.address < pb2.address;
}

void SlippiSavestate::InitBackupLocs()
{
  static std::vector<ssBackupLoc> s_full_backup_regions = {
      {0x80005520, 0x80005940, nullptr},  // Data Sections 0 and 1
      {0x803b7240, 0x804DEC00, nullptr},  // Data Sections 2-7 and in between sections including BSS

      // Full Unknown Region: [804fec00 - 80BD5C40)
      // https://docs.google.com/spreadsheets/d/16ccNK_qGrtPfx4U25w7OWIDMZ-NxN1WNBmyQhaDxnEg/edit?usp=sharing
      {0x8065c000, 0x8071b000, nullptr},  // Unknown Region Pt1
      {0x80bb0000, 0x811AD5A0, nullptr},  // Unknown Region Pt2, Heap [80bd5c40 - 811AD5A0)
  };

  static std::vector<PreserveBlock> s_exclude_sections = {
      // Sound stuff
      {0x804031A0, 0x24},     // [804031A0 - 804031C4)
      {0x80407FB4, 0x34C},    // [80407FB4 - 80408300)
      {0x80433C64, 0x1EE80},  // [80433C64 - 80452AE4)
      {0x804A8D78, 0x17A68},  // [804A8D78 - 804C07E0)
      {0x804C28E0, 0x399C},   // [804C28E0 - 804C627C)
      {0x804D7474, 0x8},      // [804D7474 - 804D747C)
      {0x804D74F0, 0x50},     // [804D74F0 - 804D7540)
      {0x804D7548, 0x4},      // [804D7548 - 804D754C)
      {0x804D7558, 0x24},     // [804D7558 - 804D757C)
      {0x804D7580, 0xC},      // [804D7580 - 804D758C)
      {0x804D759C, 0x4},      // [804D759C - 804D75A0)
      {0x804D7720, 0x4},      // [804D7720 - 804D7724)
      {0x804D7744, 0x4},      // [804D7744 - 804D7748)
      {0x804D774C, 0x8},      // [804D774C - 804D7754)
      {0x804D7758, 0x8},      // [804D7758 - 804D7760)
      {0x804D7788, 0x10},     // [804D7788 - 804D7798)
      {0x804D77C8, 0x4},      // [804D77C8 - 804D77CC)
      {0x804D77D0, 0x4},      // [804D77D0 - 804D77D4)
      {0x804D77E0, 0x4},      // [804D77E0 - 804D77E4)
      {0x804DE358, 0x80},     // [804DE358 - 804DE3D8)
      {0x804DE800, 0x70},     // [804DE800 - 804DE870)

      // The following need to be added to the ranges proper
      {0x804d6030, 0x4},   // ???
      {0x804d603c, 0x4},   // ???
      {0x804d7218, 0x4},   // ???
      {0x804d7228, 0x8},   // ???
      {0x804d7740, 0x4},   // ???
      {0x804d7754, 0x4},   // ???
      {0x804d77bc, 0x4},   // ???
      {0x804de7f0, 0x10},  // ???

      // Camera Blocks, Temporarily added here
      //{0x80452c7c, 0x2B0}, // Cam Block 1, including gaps
      //{0x806e516c, 0xA8},  // Cam Block 2, including gaps
  };

  static std::vector<ssBackupLoc> s_processed_locs = {};

  // If the processed locations are already computed, just copy them directly
  if (s_processed_locs.size())
  {
    m_backup_locs.insert(m_backup_locs.end(), s_processed_locs.begin(), s_processed_locs.end());
    return;
  }

  // Sort exclude sections
  std::sort(s_exclude_sections.begin(), s_exclude_sections.end(), cmpFn);

  // Initialize backupLocs to full regions
  m_backup_locs.insert(m_backup_locs.end(), s_full_backup_regions.begin(),
                       s_full_backup_regions.end());

  // Remove exclude sections from backupLocs
  int idx = 0;
  for (auto it = s_exclude_sections.begin(); it != s_exclude_sections.end(); ++it)
  {
    PreserveBlock ipb = *it;

    while (ipb.length > 0)
    {
      // Move up the backupLocs index until we reach a section relevant to us
      while (idx < m_backup_locs.size() && ipb.address >= m_backup_locs[idx].end_address)
      {
        idx += 1;
      }

      // Once idx is beyond backup locs, we are already not backup up this exclusion section
      if (idx >= m_backup_locs.size())
      {
        break;
      }

      // Handle case where our exclusion starts before the actual backup section
      if (ipb.address < m_backup_locs[idx].start_address)
      {
        int new_size = (s32)ipb.length - ((s32)m_backup_locs[idx].start_address - (s32)ipb.address);

        ipb.length = new_size > 0 ? new_size : 0;
        ipb.address = m_backup_locs[idx].start_address;
        continue;
      }

      // Determine new size (how much we removed from backup)
      int new_size = (s32)ipb.length - ((s32)m_backup_locs[idx].end_address - (s32)ipb.address);

      // Add split section after exclusion
      if (m_backup_locs[idx].end_address > ipb.address + ipb.length)
      {
        ssBackupLoc new_loc = {ipb.address + ipb.length, m_backup_locs[idx].end_address, nullptr};
        m_backup_locs.insert(m_backup_locs.begin() + idx + 1, new_loc);
      }

      // Modify section to end at the exclusion start
      m_backup_locs[idx].end_address = ipb.address;
      if (m_backup_locs[idx].end_address <= m_backup_locs[idx].start_address)
      {
        m_backup_locs.erase(m_backup_locs.begin() + idx);
      }

      // Set new size to see if there's still more to process
      new_size = new_size > 0 ? new_size : 0;
      ipb.address = ipb.address + (ipb.length - new_size);
      ipb.length = (u32)new_size;
    }
  }

  s_processed_locs.clear();
  s_processed_locs.insert(s_processed_locs.end(), m_backup_locs.begin(), m_backup_locs.end());
}

void SlippiSavestate::GetDolphinState(PointerWrap& p)
{
  // p.DoArray(Memory::m_pRAM, Memory::RAM_SIZE);
  // p.DoMarker("Memory");
  // VideoInterface::DoState(p);
  // p.DoMarker("VideoInterface");
  // SerialInterface::DoState(p);
  // p.DoMarker("SerialInterface");
  // ProcessorInterface::DoState(p);
  // p.DoMarker("ProcessorInterface");
  // DSP::DoState(p);
  // p.DoMarker("DSP");
  // DVDInterface::DoState(p);
  // p.DoMarker("DVDInterface");
  // GPFifo::DoState(p);
  // p.DoMarker("GPFifo");
  ExpansionInterface::DoState(p);
  p.DoMarker("ExpansionInterface");
  // AudioInterface::DoState(p);
  // p.DoMarker("AudioInterface");
}

void SlippiSavestate::Capture()
{
  // First copy memory
  for (auto it = m_backup_locs.begin(); it != m_backup_locs.end(); ++it)
  {
    auto size = it->end_address - it->start_address;
    Memory::CopyFromEmu(it->data, it->start_address, size);
  }

  //// Second copy dolphin states
  // u8 *ptr = &dolphinSsBackup[0];
  // PointerWrap p(&ptr, PointerWrap::MODE_WRITE);
  // getDolphinState(p);
}

void SlippiSavestate::Load(std::vector<PreserveBlock> blocks)
{
  // static std::vector<PreserveBlock> interruptStuff = {
  //    {0x804BF9D2, 4},
  //    {0x804C3DE4, 20},
  //    {0x804C4560, 44},
  //    {0x804D7760, 36},
  //};

  // for (auto it = interruptStuff.begin(); it != interruptStuff.end(); ++it)
  // {
  //  blocks.push_back(*it);
  // }

  // Back up
  for (auto it = blocks.begin(); it != blocks.end(); ++it)
  {
    if (!m_preservation_map.count(*it))
    {
      // TODO: Clear preservation map when game ends
      m_preservation_map[*it] = std::vector<u8>(it->length);
    }

    Memory::CopyFromEmu(&m_preservation_map[*it][0], it->address, it->length);
  }

  // Restore memory blocks
  for (auto it = m_backup_locs.begin(); it != m_backup_locs.end(); ++it)
  {
    auto size = it->end_address - it->start_address;
    Memory::CopyToEmu(it->start_address, it->data, size);
  }

  //// Restore audio
  // u8 *ptr = &dolphinSsBackup[0];
  // PointerWrap p(&ptr, PointerWrap::MODE_READ);
  // getDolphinState(p);

  // Restore
  for (auto it = blocks.begin(); it != blocks.end(); ++it)
  {
    Memory::CopyToEmu(it->address, &m_preservation_map[*it][0], it->length);
  }
}
