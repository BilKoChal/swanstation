#pragma once
#include "../file_system.h"
#include "../hash_combine.h"
#include "../types.h"
#include "../windows_headers.h"
#include "shader_compiler.h"
#include <cstdio>
#include <d3d11.h>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

namespace D3D11 {

class ShaderCache
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  ShaderCache();
  ~ShaderCache();

  void Open(std::string_view base_path, D3D_FEATURE_LEVEL feature_level, uint32_t version, bool debug);

  // Returns whether Open() has already been called successfully on
  // this instance. Used by the persistent (lazy-compile) GPU backend
  // to avoid re-reading the same on-disk index on UpdateSettings
  // round-trips through DestroyShaders/CompileShaders, which would
  // both leak file handles and double-count m_index entries.
  bool IsOpen() const { return m_index_file != nullptr; }

  ComPtr<ID3DBlob> GetShaderBlob(ShaderCompiler::Type type, std::string_view shader_code);

  ComPtr<ID3D11VertexShader> GetVertexShader(ID3D11Device* device, std::string_view shader_code);
  ComPtr<ID3D11PixelShader> GetPixelShader(ID3D11Device* device, std::string_view shader_code);

private:
  static constexpr uint32_t FILE_VERSION = 2;

  struct CacheIndexKey
  {
    uint64_t source_hash_low;
    uint64_t source_hash_high;
    uint32_t source_length;
    ShaderCompiler::Type shader_type;

    bool operator==(const CacheIndexKey& key) const;
  };

  struct CacheIndexEntryHasher
  {
    std::size_t operator()(const CacheIndexKey& e) const noexcept
    {
      std::size_t h = 0;
      hash_combine(h, e.source_hash_low, e.source_hash_high, e.source_length, e.shader_type);
      return h;
    }
  };

  struct CacheIndexData
  {
    uint32_t file_offset;
    uint32_t blob_size;
  };

  using CacheIndex = std::unordered_map<CacheIndexKey, CacheIndexData, CacheIndexEntryHasher>;

  static std::string GetCacheBaseFileName(const std::string_view& base_path, D3D_FEATURE_LEVEL feature_level,
                                          bool debug);
  static CacheIndexKey GetCacheKey(ShaderCompiler::Type type, const std::string_view& shader_code);

  bool CreateNew(const std::string& index_filename, const std::string& blob_filename);
  bool ReadExisting(const std::string& index_filename, const std::string& blob_filename);
  void Close();

  ComPtr<ID3DBlob> CompileAndAddShaderBlob(const CacheIndexKey& key, std::string_view shader_code);

  RFILE* m_index_file = nullptr;
  RFILE* m_blob_file = nullptr;

  CacheIndex m_index;

  D3D_FEATURE_LEVEL m_feature_level = D3D_FEATURE_LEVEL_11_0;
  uint32_t m_version = 0;
  bool m_debug = false;
};

} // namespace D3D11
