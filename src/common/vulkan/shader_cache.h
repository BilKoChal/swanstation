#pragma once
#include "../file_system.h"
#include "../hash_combine.h"
#include "../types.h"
#include "shader_compiler.h"
#include "vulkan_loader.h"
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vulkan {

class ShaderCache
{
public:
  ~ShaderCache();

  static void Create(std::string_view base_path, uint32_t version, bool debug);
  static void Destroy();

  /// Returns a handle to the pipeline cache. Set set_dirty to true if you are planning on writing to it externally.
  VkPipelineCache GetPipelineCache(bool set_dirty = true);

  /// Writes pipeline cache to file, saving all newly compiled pipelines.
  bool FlushPipelineCache();

  std::optional<ShaderCompiler::SPIRVCodeVector> GetShaderSPV(ShaderCompiler::Type type, std::string_view shader_code);
  VkShaderModule GetShaderModule(ShaderCompiler::Type type, std::string_view shader_code);

  VkShaderModule GetVertexShader(std::string_view shader_code);
  VkShaderModule GetFragmentShader(std::string_view shader_code);

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

  ShaderCache();

  static std::string GetShaderCacheBaseFileName(const std::string_view& base_path, bool debug);
  static std::string GetPipelineCacheBaseFileName(const std::string_view& base_path, bool debug);
  static CacheIndexKey GetCacheKey(ShaderCompiler::Type type, const std::string_view& shader_code);

  void Open(std::string_view base_path, uint32_t version, bool debug);

  bool CreateNewShaderCache(const std::string& index_filename, const std::string& blob_filename);
  bool ReadExistingShaderCache(const std::string& index_filename, const std::string& blob_filename);
  void CloseShaderCache();

  bool CreateNewPipelineCache();
  bool ReadExistingPipelineCache();
  void ClosePipelineCache();

  std::optional<ShaderCompiler::SPIRVCodeVector> CompileAndAddShaderSPV(const CacheIndexKey& key,
                                                                        std::string_view shader_code);

  RFILE* m_index_file = nullptr;
  RFILE* m_blob_file = nullptr;
  std::string m_pipeline_cache_filename;

  // Protects m_index (the unordered_map), m_blob_file (rfseek/rfread
  // share file position state), and m_index_file. NOT held across
  // the slow ShaderCompiler::CompileShader (glslang -> SPIR-V) call
  // - that runs lock-free so two threads compiling different shaders
  // truly run in parallel. Two threads racing to compile the same
  // shader is harmless: glslang is deterministic on identical source,
  // so both produce identical SPIR-V; the double-check in
  // CompileAndAddShaderSPV picks whichever publishes first.
  std::mutex m_shader_cache_mutex;

  // Serialises external access to m_pipeline_cache. Per the Vulkan
  // spec, the pipelineCache parameter to vkCreateGraphicsPipelines /
  // vkCreateComputePipelines / vkMergePipelineCaches is in the host-
  // synchronisation parameter list - the application must guarantee
  // no concurrent use of the same VkPipelineCache. (The
  // VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT flag from
  // Vulkan 1.3 / VK_EXT_pipeline_creation_cache_control would
  // confirm the contract to the driver; without it the driver is
  // permitted to assume serial access.)
  //
  // Lazy-fault PSO compile helpers in GPU_HW_Vulkan acquire this via
  // PipelineCacheMutex() around their gpbuilder.Create(...) call.
  // The actual SPIR-V -> GPU-ISA compile inside the driver is
  // typically offloaded to the driver's own background threads, so
  // the lock window per call is short relative to the glslang
  // compile - and crucially, the glslang compile (which is the
  // slow part on shaders that hit D3DCompile-style optimiser
  // pathological cases) is now outside this lock entirely.
  std::mutex m_pipeline_cache_mutex;

public:
  // Exposed so lazy-fault PSO compile helpers can synchronise their
  // vkCreateGraphicsPipelines call against any other thread also
  // creating pipelines with the same VkPipelineCache. Returned
  // by-reference; lifetime tied to the ShaderCache singleton.
  std::mutex& PipelineCacheMutex() { return m_pipeline_cache_mutex; }

private:

  CacheIndex m_index;

  VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
  uint32_t m_version = 0;
  bool m_debug = false;
  bool m_pipeline_cache_dirty = false;
};

} // namespace Vulkan

extern std::unique_ptr<Vulkan::ShaderCache> g_vulkan_shader_cache;
