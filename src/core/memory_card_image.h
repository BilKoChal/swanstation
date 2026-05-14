#pragma once
#include "common/bitfield.h"
#include "controller.h"
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MemoryCardImage {

inline constexpr uint32_t DATA_SIZE = 128 * 1024, // 1mbit
  BLOCK_SIZE = 8192, FRAME_SIZE = 128, FRAMES_PER_BLOCK = BLOCK_SIZE / FRAME_SIZE, NUM_BLOCKS = DATA_SIZE / BLOCK_SIZE,
                     NUM_FRAMES = DATA_SIZE / FRAME_SIZE, ICON_WIDTH = 16, ICON_HEIGHT = 16;

using DataArray = std::array<uint8_t, DATA_SIZE>;

bool LoadFromFile(DataArray* data, const char* filename);
bool SaveToFile(const DataArray& data, const char* filename);

void Format(DataArray* data);

struct IconFrame
{
  uint32_t pixels[ICON_WIDTH * ICON_HEIGHT];
};

struct FileInfo
{
  std::string filename;
  std::string title;
  uint32_t size;
  uint32_t first_block;
  uint32_t num_blocks;
  bool deleted;

  std::vector<IconFrame> icon_frames;
};

bool IsValid(const DataArray& data);
} // namespace MemoryCardImage
