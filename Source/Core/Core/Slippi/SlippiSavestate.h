#pragma once

#include <unordered_map>
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"

class PointerWrap;

class SlippiSavestate
{
public:
  struct PreserveBlock
  {
    bool operator==(const PreserveBlock& p) const
    {
      return address == p.address && length == p.length;
    }
    u32 address;
    u32 length;
  };

  SlippiSavestate();
  ~SlippiSavestate();

  void Capture();
  void Load(std::vector<PreserveBlock> blocks);

private:
  typedef struct
  {
    u32 start_address;
    u32 end_address;
    u8* data;
  } ssBackupLoc;

  typedef struct
  {
    u32 address;
    u32 value;
  } ssBackupStaticToHeapPtr;

  struct preserve_hash_fn
  {
    std::size_t operator()(const PreserveBlock& node) const
    {
      return node.address ^ node.length;  // TODO: This is probably a bad hash
    }
  };

  void InitBackupLocs();
  void GetDolphinState(PointerWrap& p);

  // These are the game locations to back up and restore
  std::vector<ssBackupLoc> m_backup_locs = {};
  std::unordered_map<PreserveBlock, std::vector<u8>, preserve_hash_fn> m_preservation_map;
  std::vector<u8> m_dolphin_ss_backup;
};
