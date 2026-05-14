#pragma once
#include "cd_image.h"
#include "types.h"
#include <array>
#include <unordered_map>

class CDSubChannelReplacement
{
public:
  CDSubChannelReplacement();
  ~CDSubChannelReplacement();

  uint32_t GetReplacementSectorCount() const { return static_cast<uint32_t>(m_replacement_subq.size()); }

  bool LoadSBI(const char* path);
  bool LoadSBIFromImagePath(const char* image_path);

  /// Adds a sector to the replacement map.
  void AddReplacementSubChannelQ(uint32_t lba, const CDImage::SubChannelQ& subq);

  /// Returns the replacement subchannel data for the specified position (in BCD).
  bool GetReplacementSubChannelQ(uint8_t minute_bcd, uint8_t second_bcd, uint8_t frame_bcd, CDImage::SubChannelQ* subq) const;

  /// Returns the replacement subchannel data for the specified sector.
  bool GetReplacementSubChannelQ(uint32_t lba, CDImage::SubChannelQ* subq) const;

private:
  using ReplacementMap = std::unordered_map<uint32_t, CDImage::SubChannelQ>;

  ReplacementMap m_replacement_subq;
};
