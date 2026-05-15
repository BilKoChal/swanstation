#pragma once
#include "../file_system.h"
#include "../hash_combine.h"
#include "../types.h"
#include "../windows_headers.h"
#include <cstdio>
#include <d3d12.h>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

namespace D3D12 {

class ShaderCache
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  enum class EntryType
  {
    VertexShader,
    GeometryShader,
    PixelShader,
    ComputeShader,
    GraphicsPipeline,
  };

  ShaderCache();
  ~ShaderCache();

  void Open(std::string_view base_path, D3D_FEATURE_LEVEL feature_level, bool debug);

  // Returns whether Open() has already been called successfully on
  // this instance. Used by the persistent (lazy-compile) GPU backend
  // to avoid re-reading the same on-disk index on UpdateSettings
  // round-trips through DestroyPipelines/CompilePipelines, which
  // would both leak file handles and double-count m_shader_index /
  // m_pipeline_index entries. Mirrors the equivalent accessor on
  // D3D11::ShaderCache.
  bool IsOpen() const { return m_shader_index_file != nullptr; }

  ALWAYS_INLINE ComPtr<ID3DBlob> GetVertexShader(std::string_view shader_code)
  {
    return GetShaderBlob(EntryType::VertexShader, shader_code);
  }
  ALWAYS_INLINE ComPtr<ID3DBlob> GetPixelShader(std::string_view shader_code)
  {
    return GetShaderBlob(EntryType::PixelShader, shader_code);
  }

  ComPtr<ID3DBlob> GetShaderBlob(EntryType type, std::string_view shader_code);

  ComPtr<ID3D12PipelineState> GetPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);

private:
  static constexpr uint32_t FILE_VERSION = 1;

  struct CacheIndexKey
  {
    uint64_t source_hash_low;
    uint64_t source_hash_high;
    uint32_t source_length;
    EntryType type;

    bool operator==(const CacheIndexKey& key) const;
  };

  struct CacheIndexEntryHasher
  {
    std::size_t operator()(const CacheIndexKey& e) const noexcept
    {
      std::size_t h = 0;
      hash_combine(h, e.source_hash_low, e.source_hash_high, e.source_length, e.type);
      return h;
    }
  };

  struct CacheIndexData
  {
    uint32_t file_offset;
    uint32_t blob_size;
  };

  using CacheIndex = std::unordered_map<CacheIndexKey, CacheIndexData, CacheIndexEntryHasher>;

  static std::string GetCacheBaseFileName(const std::string_view& base_path, const std::string_view& type,
                                          D3D_FEATURE_LEVEL feature_level, bool debug);
  static CacheIndexKey GetShaderCacheKey(EntryType type, const std::string_view& shader_code);
  static CacheIndexKey GetPipelineCacheKey(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& gpdesc);

  bool CreateNew(const std::string& index_filename, const std::string& blob_filename, RFILE*& index_file,
                 RFILE*& blob_file);
  bool ReadExisting(const std::string& index_filename, const std::string& blob_filename, RFILE*& index_file,
                    RFILE*& blob_file, CacheIndex& index);
  void InvalidatePipelineCache();
  void Close();

  ComPtr<ID3DBlob> CompileAndAddShaderBlob(const CacheIndexKey& key, std::string_view shader_code);
  ComPtr<ID3D12PipelineState> CompileAndAddPipeline(ID3D12Device* device, const CacheIndexKey& key,
                                                    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& gpdesc);

  std::string m_base_path;

  // Two mutexes covering the two cache sides (HLSL shader blobs vs
  // D3D12 pipeline blobs). The mutexes are deliberately NOT held
  // across the slow operations - D3DCompile in
  // CompileAndAddShaderBlob and CreateGraphicsPipelineState in
  // CompileAndAddPipeline - so two threads compiling different
  // shaders / pipelines run truly in parallel.
  //
  // Two threads racing to compile the SAME shader is harmless: each
  // does its own D3DCompile, then takes the mutex to publish the
  // result. The second one observes the slot already filled (via the
  // double-check inside CompileAndAddXxx) and discards its copy. The
  // wasted compile is the cost of avoiding the deadlock the previous
  // single-mutex-around-everything design would have caused.
  std::mutex m_shader_cache_mutex;
  std::mutex m_pipeline_cache_mutex;

  RFILE* m_shader_index_file = nullptr;
  RFILE* m_shader_blob_file = nullptr;
  CacheIndex m_shader_index;

  RFILE* m_pipeline_index_file = nullptr;
  RFILE* m_pipeline_blob_file = nullptr;
  CacheIndex m_pipeline_index;

  D3D_FEATURE_LEVEL m_feature_level = D3D_FEATURE_LEVEL_11_0;
  bool m_use_pipeline_cache = false;
  bool m_debug = false;
};

} // namespace D3D12
