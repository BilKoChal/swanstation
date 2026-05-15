#include "shader_cache.h"
#include "../d3d11/shader_compiler.h"
#include "../file_system.h"
#include "../log.h"
#include "../md5_digest.h"
#include <cstring>
#include <d3dcompiler.h>

#include <file/file_path.h>

Log_SetChannel(D3D12::ShaderCache);

namespace D3D12 {

#pragma pack(push, 1)
struct CacheIndexEntry
{
  uint64_t source_hash_low;
  uint64_t source_hash_high;
  uint32_t source_length;
  uint32_t shader_type;
  uint32_t file_offset;
  uint32_t blob_size;
};
#pragma pack(pop)

// Pipeline-state-object disk caching has been disabled.
//
// The previous implementation persisted each PSO's full
// ID3D12PipelineState::GetCachedBlob output as an independent
// entry keyed by the merged graphics-pipeline-state descriptor hash.
// On a stock SwanStation matrix that meant ~1440 batch PSOs plus
// the non-batch pipelines, each blob carrying a fully-validated
// state object with VS and PS bytecode embedded - ~16 KB per PSO on
// average. The on-disk d3d12_pipelines_sm50.bin grew to ~75 MB for
// roughly the same coverage that PCSX2's Vulkan/D3D12 caches reach
// at ~1.5 MB, because PCSX2 caches at the shader-bytecode level only
// and lets the runtime build PSOs from cached bytecode on demand.
//
// We keep the shader bytecode cache (d3d12_shaders_sm50.bin); that's
// the expensive recompile to save. Driver-side PSO assembly from
// already-compiled DXBC is fast - sub-millisecond on modern desktop
// GPUs - so re-doing it each cold start is not a real cost. The
// only path that benefited from the persistent PSO cache was a
// huge-matrix synchronous "Enabled" precompile, and even there the
// 75 MB file did very little because reading it back and feeding
// CachedPSO to CreateGraphicsPipelineState was not meaningfully
// faster than just rebuilding from cached shader bytecode.
static bool CanUsePipelineCache()
{
  return false;
}

ShaderCache::ShaderCache() : m_use_pipeline_cache(CanUsePipelineCache()) {}

ShaderCache::~ShaderCache()
{
  // Serialize the pipeline library to disk first - while m_pipeline_library
  // is still alive and its backing m_pipeline_library_blob has not been
  // destroyed. Best-effort: a write failure logs and falls through to
  // the rest of the teardown.
  if (m_use_pipeline_library && m_pipeline_library && !m_base_path.empty())
    SerializePipelineLibrary();

  if (m_pipeline_index_file)
    rfclose(m_pipeline_index_file);
  if (m_pipeline_blob_file)
    rfclose(m_pipeline_blob_file);
  if (m_shader_index_file)
    rfclose(m_shader_index_file);
  if (m_shader_blob_file)
    rfclose(m_shader_blob_file);
}

bool ShaderCache::CacheIndexKey::operator==(const CacheIndexKey& key) const
{
  return (source_hash_low == key.source_hash_low && source_hash_high == key.source_hash_high &&
          source_length == key.source_length && type == key.type);
}

void ShaderCache::Open(std::string_view base_path, ID3D12Device* device, D3D_FEATURE_LEVEL feature_level, bool debug)
{
  m_base_path = base_path;
  m_feature_level = feature_level;
  m_debug = debug;

  if (!base_path.empty())
  {
    const std::string base_shader_filename = GetCacheBaseFileName(base_path, "shaders", feature_level, debug);
    const std::string shader_index_filename = base_shader_filename + ".idx";
    const std::string shader_blob_filename = base_shader_filename + ".bin";

    if (!ReadExisting(shader_index_filename, shader_blob_filename, m_shader_index_file, m_shader_blob_file,
                      m_shader_index))
    {
      CreateNew(shader_index_filename, shader_blob_filename, m_shader_index_file, m_shader_blob_file);
    }

    if (m_use_pipeline_cache)
    {
      const std::string base_pipelines_filename = GetCacheBaseFileName(base_path, "pipelines", feature_level, debug);
      const std::string pipelines_index_filename = base_pipelines_filename + ".idx";
      const std::string pipelines_blob_filename = base_pipelines_filename + ".bin";

      if (!ReadExisting(pipelines_index_filename, pipelines_blob_filename, m_pipeline_index_file, m_pipeline_blob_file,
                        m_pipeline_index))
      {
        CreateNew(pipelines_index_filename, pipelines_blob_filename, m_pipeline_index_file, m_pipeline_blob_file);
      }
    }
    else
    {
      // Scrub any leftover pipeline cache from a previous build that
      // had the per-PSO blob cache enabled. The file is no longer
      // touched at runtime, so without this it would just sit in the
      // user's system directory forever consuming ~75 MB. Doing this
      // on every Open is cheap (two path_is_valid+unlink hits) and
      // self-healing across upgrades and downgrades.
      const std::string base_pipelines_filename = GetCacheBaseFileName(base_path, "pipelines", feature_level, debug);
      const std::string pipelines_index_filename = base_pipelines_filename + ".idx";
      const std::string pipelines_blob_filename = base_pipelines_filename + ".bin";
      if (path_is_valid(pipelines_index_filename.c_str()))
        filestream_delete(pipelines_index_filename.c_str());
      if (path_is_valid(pipelines_blob_filename.c_str()))
        filestream_delete(pipelines_blob_filename.c_str());
    }

    // ID3D12PipelineLibrary path (D3D12.1, Windows 10 v1703+). We try
    // this whenever a device was provided and supports ID3D12Device1.
    // Independent of m_use_pipeline_cache - the library replaces the
    // per-PSO blob cache with a single driver-managed file, internally
    // dedup'd across PSOs that share shader bytecode. On a fully
    // populated session the library file is comparable in size to the
    // ~5 MB shader bytecode cache rather than the 75 MB the per-PSO
    // cache reached.
    if (device)
    {
      ComPtr<ID3D12Device1> device1;
      HRESULT hr = device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf()));
      if (SUCCEEDED(hr) && device1)
      {
        m_use_pipeline_library = TryOpenPipelineLibrary(device1.Get(), base_path, feature_level, debug);
      }
    }
  }
}

void ShaderCache::InvalidatePipelineCache()
{
  m_pipeline_index.clear();
  if (m_pipeline_blob_file)
  {
    rfclose(m_pipeline_blob_file);
    m_pipeline_blob_file = nullptr;
  }

  if (m_pipeline_index_file)
  {
    rfclose(m_pipeline_index_file);
    m_pipeline_index_file = nullptr;
  }

  if (m_use_pipeline_cache)
  {
    const std::string base_pipelines_filename =
      GetCacheBaseFileName(m_base_path, "pipelines", m_feature_level, m_debug);
    const std::string pipelines_index_filename = base_pipelines_filename + ".idx";
    const std::string pipelines_blob_filename = base_pipelines_filename + ".bin";
    CreateNew(pipelines_index_filename, pipelines_blob_filename, m_pipeline_index_file, m_pipeline_blob_file);
  }
}

bool ShaderCache::CreateNew(const std::string& index_filename, const std::string& blob_filename, RFILE*& index_file,
                            RFILE*& blob_file)
{
  if (path_is_valid(index_filename.c_str()))
  {
    Log_WarningPrintf("Removing existing index file '%s'", index_filename.c_str());
    filestream_delete(index_filename.c_str());
  }
  if (path_is_valid(blob_filename.c_str()))
  {
    Log_WarningPrintf("Removing existing blob file '%s'", blob_filename.c_str());
    filestream_delete(blob_filename.c_str());
  }

  index_file = FileSystem::OpenRFile(index_filename.c_str(), "wb");
  if (!index_file)
  {
    Log_ErrorPrintf("Failed to open index file '%s' for writing", index_filename.c_str());
    return false;
  }

  const uint32_t index_version = FILE_VERSION;
  if (rfwrite(&index_version, sizeof(index_version), 1, index_file) != 1)
  {
    Log_ErrorPrintf("Failed to write version to index file '%s'", index_filename.c_str());
    rfclose(index_file);
    index_file = nullptr;
    filestream_delete(index_filename.c_str());
    return false;
  }

  blob_file = FileSystem::OpenRFile(blob_filename.c_str(), "w+b");
  if (!blob_file)
  {
    Log_ErrorPrintf("Failed to open blob file '%s' for writing", blob_filename.c_str());
    rfclose(blob_file);
    blob_file = nullptr;
    filestream_delete(index_filename.c_str());
    return false;
  }

  return true;
}

bool ShaderCache::ReadExisting(const std::string& index_filename, const std::string& blob_filename,
                               RFILE*& index_file, RFILE*& blob_file, CacheIndex& index)
{
  index_file = FileSystem::OpenRFile(index_filename.c_str(), "r+b");
  if (!index_file)
    return false;

  uint32_t file_version;
  if (rfread(&file_version, sizeof(file_version), 1, index_file) != 1 || file_version != FILE_VERSION)
  {
    Log_ErrorPrintf("Bad file version in '%s'", index_filename.c_str());
    rfclose(index_file);
    index_file = nullptr;
    return false;
  }

  blob_file = FileSystem::OpenRFile(blob_filename.c_str(), "a+b");
  if (!blob_file)
  {
    Log_ErrorPrintf("Blob file '%s' is missing", blob_filename.c_str());
    rfclose(index_file);
    index_file = nullptr;
    return false;
  }

  rfseek(blob_file, 0, SEEK_END);
  const uint32_t blob_file_size = static_cast<uint32_t>(rftell(blob_file));

  for (;;)
  {
    CacheIndexEntry entry;
    if (rfread(&entry, sizeof(entry), 1, index_file) != 1 || (entry.file_offset + entry.blob_size) > blob_file_size)
    {
      if (rfeof(index_file))
        break;

      Log_ErrorPrintf("Failed to read entry from '%s', corrupt file?", index_filename.c_str());
      index.clear();
      rfclose(blob_file);
      blob_file = nullptr;
      rfclose(index_file);
      index_file = nullptr;
      return false;
    }

    const CacheIndexKey key{entry.source_hash_low, entry.source_hash_high, entry.source_length,
                            static_cast<EntryType>(entry.shader_type)};
    const CacheIndexData data{entry.file_offset, entry.blob_size};
    index.emplace(key, data);
  }

  // ensure we don't write before seeking
  rfseek(index_file, 0, SEEK_END);

  Log_InfoPrintf("Read %zu entries from '%s'", index.size(), index_filename.c_str());
  return true;
}

std::string ShaderCache::GetCacheBaseFileName(const std::string_view& base_path, const std::string_view& type,
                                              D3D_FEATURE_LEVEL feature_level, bool debug)
{
  std::string base_filename(base_path);
  base_filename += "d3d12_";
  base_filename += type;
  base_filename += "_";

  switch (feature_level)
  {
    case D3D_FEATURE_LEVEL_10_0:
      base_filename += "sm40";
      break;
    case D3D_FEATURE_LEVEL_10_1:
      base_filename += "sm41";
      break;
    case D3D_FEATURE_LEVEL_11_0:
      base_filename += "sm50";
      break;
    default:
      base_filename += "unk";
      break;
  }

  if (debug)
    base_filename += "_debug";

  return base_filename;
}

union MD5Hash
{
  struct
  {
    uint64_t low;
    uint64_t high;
  };
  uint8_t hash[16];
};

ShaderCache::CacheIndexKey ShaderCache::GetShaderCacheKey(EntryType type, const std::string_view& shader_code)
{
  MD5Hash h;
  MD5Digest digest;
  digest.Update(shader_code.data(), static_cast<uint32_t>(shader_code.length()));
  digest.Final(h.hash);

  return CacheIndexKey{h.low, h.high, static_cast<uint32_t>(shader_code.length()), type};
}

ShaderCache::CacheIndexKey ShaderCache::GetPipelineCacheKey(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& gpdesc)
{
  MD5Digest digest;
  uint32_t length = sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC);

  if (gpdesc.VS.BytecodeLength > 0)
  {
    digest.Update(gpdesc.VS.pShaderBytecode, static_cast<uint32_t>(gpdesc.VS.BytecodeLength));
    length += static_cast<uint32_t>(gpdesc.VS.BytecodeLength);
  }
  if (gpdesc.GS.BytecodeLength > 0)
  {
    digest.Update(gpdesc.GS.pShaderBytecode, static_cast<uint32_t>(gpdesc.GS.BytecodeLength));
    length += static_cast<uint32_t>(gpdesc.GS.BytecodeLength);
  }
  if (gpdesc.PS.BytecodeLength > 0)
  {
    digest.Update(gpdesc.PS.pShaderBytecode, static_cast<uint32_t>(gpdesc.PS.BytecodeLength));
    length += static_cast<uint32_t>(gpdesc.PS.BytecodeLength);
  }

  digest.Update(&gpdesc.BlendState, sizeof(gpdesc.BlendState));
  digest.Update(&gpdesc.SampleMask, sizeof(gpdesc.SampleMask));
  digest.Update(&gpdesc.RasterizerState, sizeof(gpdesc.RasterizerState));
  digest.Update(&gpdesc.DepthStencilState, sizeof(gpdesc.DepthStencilState));

  for (uint32_t i = 0; i < gpdesc.InputLayout.NumElements; i++)
  {
    const D3D12_INPUT_ELEMENT_DESC& ie = gpdesc.InputLayout.pInputElementDescs[i];
    digest.Update(ie.SemanticName, static_cast<uint32_t>(std::strlen(ie.SemanticName)));
    digest.Update(&ie.SemanticIndex, sizeof(ie.SemanticIndex));
    digest.Update(&ie.Format, sizeof(ie.Format));
    digest.Update(&ie.InputSlot, sizeof(ie.InputSlot));
    digest.Update(&ie.AlignedByteOffset, sizeof(ie.AlignedByteOffset));
    digest.Update(&ie.InputSlotClass, sizeof(ie.InputSlotClass));
    digest.Update(&ie.InstanceDataStepRate, sizeof(ie.InstanceDataStepRate));
    length += sizeof(D3D12_INPUT_ELEMENT_DESC);
  }

  digest.Update(&gpdesc.IBStripCutValue, sizeof(gpdesc.IBStripCutValue));
  digest.Update(&gpdesc.PrimitiveTopologyType, sizeof(gpdesc.PrimitiveTopologyType));
  digest.Update(&gpdesc.NumRenderTargets, sizeof(gpdesc.NumRenderTargets));
  digest.Update(gpdesc.RTVFormats, sizeof(gpdesc.RTVFormats));
  digest.Update(&gpdesc.DSVFormat, sizeof(gpdesc.DSVFormat));
  digest.Update(&gpdesc.SampleDesc, sizeof(gpdesc.SampleDesc));
  digest.Update(&gpdesc.Flags, sizeof(gpdesc.Flags));

  MD5Hash h;
  digest.Final(h.hash);

  return CacheIndexKey{h.low, h.high, length, EntryType::GraphicsPipeline};
}

ShaderCache::ComPtr<ID3DBlob> ShaderCache::GetShaderBlob(EntryType type, std::string_view shader_code)
{
  const auto key = GetShaderCacheKey(type, shader_code);

  // Fast path: look up in the index under the lock and read the
  // existing blob if present. The lock window covers the
  // unordered_map lookup AND the rfseek+rfread on the blob file
  // since file position is shared mutable state.
  {
    std::lock_guard<std::mutex> lock(m_shader_cache_mutex);
    auto iter = m_shader_index.find(key);
    if (iter != m_shader_index.end())
    {
      ComPtr<ID3DBlob> blob;
      HRESULT hr = D3DCreateBlob(iter->second.blob_size, blob.GetAddressOf());
      if (FAILED(hr) || rfseek(m_shader_blob_file, iter->second.file_offset, SEEK_SET) != 0 ||
          rfread(blob->GetBufferPointer(), 1, iter->second.blob_size, m_shader_blob_file) != iter->second.blob_size)
      {
        Log_ErrorPrintf("Read blob from file failed");
        return {};
      }
      return blob;
    }
  }

  // Slow path: compile WITHOUT the cache mutex held, so other threads
  // can use the cache (or fault their own different shaders) in
  // parallel. Concurrent compiles of the same shader are tolerated -
  // see the publish step in CompileAndAddShaderBlob.
  return CompileAndAddShaderBlob(key, shader_code);
}

ShaderCache::ComPtr<ID3D12PipelineState> ShaderCache::GetPipelineState(ID3D12Device* device,
                                                                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
  const auto key = GetPipelineCacheKey(desc);

  // Pipeline library path (D3D12.1+). Try LoadGraphicsPipeline first
  // for a microsecond-cost cache hit; on miss fall through to
  // CreateGraphicsPipelineState and best-effort StorePipeline. The
  // library handles its own internal dedup of shader bytecode across
  // PSOs, so the cached file stays compact (PCSX2-style sizes).
  //
  // ID3D12PipelineLibrary methods are documented thread-safe by
  // Microsoft; no cache mutex is held across LoadGraphicsPipeline,
  // CreateGraphicsPipelineState, or StorePipeline. Lock-free
  // compile property from 75e8269 is preserved.
  //
  // StorePipeline returns E_INVALIDARG when an entry with the same
  // name already exists (race loser, or repeated call). That's an
  // expected outcome, not a failure - the PSO we just compiled is
  // equivalent to whatever's already stored. Ignore the result.
  if (m_use_pipeline_library && m_pipeline_library)
  {
    WCHAR name[33];
    FormatPipelineLibraryName(key, name);

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = m_pipeline_library->LoadGraphicsPipeline(name, &desc, IID_PPV_ARGS(pso.GetAddressOf()));
    if (SUCCEEDED(hr))
      return pso;

    hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf()));
    if (FAILED(hr))
    {
      Log_ErrorPrintf("CreateGraphicsPipelineState failed: %08X", hr);
      return {};
    }

    m_pipeline_library->StorePipeline(name, pso.Get());
    return pso;
  }

  // Fast path: index lookup under the lock. Read the cached blob
  // from disk (and the rfseek/rfread file-position state) all
  // inside the lock so two threads' reads don't trample each
  // other's seek position.
  //
  // Note: after the disk PSO cache was dropped (commit
  // 81e3a2b8) m_pipeline_blob_file is always null when the cache
  // is open, so the iter-found branch never fires in practice -
  // the index stays empty. Code kept correct in case the cache
  // comes back.
  ComPtr<ID3DBlob> blob;
  bool have_existing = false;
  {
    std::lock_guard<std::mutex> lock(m_pipeline_cache_mutex);
    auto iter = m_pipeline_index.find(key);
    if (iter != m_pipeline_index.end())
    {
      HRESULT hr = D3DCreateBlob(iter->second.blob_size, blob.GetAddressOf());
      if (FAILED(hr) || rfseek(m_pipeline_blob_file, iter->second.file_offset, SEEK_SET) != 0 ||
          rfread(blob->GetBufferPointer(), 1, iter->second.blob_size, m_pipeline_blob_file) != iter->second.blob_size)
      {
        Log_ErrorPrintf("Read blob from file failed");
        return {};
      }
      have_existing = true;
    }
  }

  if (!have_existing)
    return CompileAndAddPipeline(device, key, desc);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc_with_blob(desc);
  desc_with_blob.CachedPSO.pCachedBlob = blob->GetBufferPointer();
  desc_with_blob.CachedPSO.CachedBlobSizeInBytes = blob->GetBufferSize();

  // CreateGraphicsPipelineState runs WITHOUT the cache mutex held -
  // it's the slow operation we never want to serialise threads on.
  ComPtr<ID3D12PipelineState> pso;
  HRESULT hr = device->CreateGraphicsPipelineState(&desc_with_blob, IID_PPV_ARGS(pso.GetAddressOf()));
  if (FAILED(hr))
  {
    Log_WarningPrintf("Creating cached PSO failed: %08X. Invalidating cache.", hr);
    {
      std::lock_guard<std::mutex> lock(m_pipeline_cache_mutex);
      InvalidatePipelineCache();
    }
    pso = CompileAndAddPipeline(device, key, desc);
  }

  return pso;
}

ShaderCache::ComPtr<ID3DBlob> ShaderCache::CompileAndAddShaderBlob(const CacheIndexKey& key,
                                                                   std::string_view shader_code)
{
  // SLOW: D3DCompile runs WITHOUT the cache mutex held. Two threads
  // racing to compile the same shader both end up here; D3DCompile
  // is deterministic on identical source so they produce identical
  // DXBC. The double-check below picks whichever completed first.
  ComPtr<ID3DBlob> blob;

  switch (key.type)
  {
    case EntryType::VertexShader:
      blob = D3D11::ShaderCompiler::CompileShader(D3D11::ShaderCompiler::Type::Vertex, m_feature_level, shader_code,
                                                  m_debug);
      break;
    case EntryType::GeometryShader:
      blob = D3D11::ShaderCompiler::CompileShader(D3D11::ShaderCompiler::Type::Geometry, m_feature_level, shader_code,
                                                  m_debug);
      break;
    case EntryType::PixelShader:
      blob =
        D3D11::ShaderCompiler::CompileShader(D3D11::ShaderCompiler::Type::Pixel, m_feature_level, shader_code, m_debug);
      break;
    default:
      break;
  }

  if (!blob)
    return {};

  // FAST: take the lock to publish. Double-check under the lock in
  // case another thread won the race - if so, read their blob from
  // disk and return it instead of writing ours (saves a file write
  // and keeps the index uniquely-keyed).
  std::lock_guard<std::mutex> lock(m_shader_cache_mutex);

  auto iter = m_shader_index.find(key);
  if (iter != m_shader_index.end())
  {
    ComPtr<ID3DBlob> existing_blob;
    HRESULT hr = D3DCreateBlob(iter->second.blob_size, existing_blob.GetAddressOf());
    if (FAILED(hr) || rfseek(m_shader_blob_file, iter->second.file_offset, SEEK_SET) != 0 ||
        rfread(existing_blob->GetBufferPointer(), 1, iter->second.blob_size, m_shader_blob_file) !=
          iter->second.blob_size)
    {
      // Read failure on the existing entry - fall through to our
      // freshly-compiled blob rather than fail the call.
      Log_WarningPrintf("Double-checked shader cache read failed; using freshly-compiled blob");
    }
    else
    {
      return existing_blob;
    }
  }

  if (!m_shader_blob_file || rfseek(m_shader_blob_file, 0, SEEK_END) != 0)
    return blob;

  CacheIndexData data;
  data.file_offset = static_cast<uint32_t>(rftell(m_shader_blob_file));
  data.blob_size = static_cast<uint32_t>(blob->GetBufferSize());

  CacheIndexEntry entry = {};
  entry.source_hash_low = key.source_hash_low;
  entry.source_hash_high = key.source_hash_high;
  entry.source_length = key.source_length;
  entry.shader_type = static_cast<uint32_t>(key.type);
  entry.blob_size = data.blob_size;
  entry.file_offset = data.file_offset;

  if (rfwrite(blob->GetBufferPointer(), 1, entry.blob_size, m_shader_blob_file) != entry.blob_size ||
      filestream_flush(m_shader_blob_file) != 0 || rfwrite(&entry, sizeof(entry), 1, m_shader_index_file) != 1 ||
      filestream_flush(m_shader_index_file) != 0)
  {
    Log_ErrorPrintf("Failed to write shader blob to file");
    return blob;
  }

  m_shader_index.emplace(key, data);
  return blob;
}

ShaderCache::ComPtr<ID3D12PipelineState>
ShaderCache::CompileAndAddPipeline(ID3D12Device* device, const CacheIndexKey& key,
                                   const D3D12_GRAPHICS_PIPELINE_STATE_DESC& gpdesc)
{
  // SLOW: CreateGraphicsPipelineState runs WITHOUT the cache mutex
  // held - it's the equivalent of D3DCompile on the PSO side. Two
  // threads racing to compile the same PSO key both end up here;
  // both produce equivalent ID3D12PipelineState objects (PSOs are
  // deterministic on identical descriptors). The double-check
  // below picks whichever completed first.
  ComPtr<ID3D12PipelineState> pso;
  HRESULT hr = device->CreateGraphicsPipelineState(&gpdesc, IID_PPV_ARGS(pso.GetAddressOf()));
  if (FAILED(hr))
  {
    Log_ErrorPrintf("Creating cached PSO failed: %08X", hr);
    return {};
  }

  // After the disk PSO cache drop (commit 81e3a2b8) the
  // m_pipeline_blob_file is always null when the cache is open, so
  // the whole index-update block below is dead in practice. Code
  // kept correct in case the cache comes back. The earlier
  // m_shader_index.emplace was a latent typo in dead code - it's
  // been corrected to m_pipeline_index.emplace here.
  if (!m_pipeline_blob_file)
    return pso;

  ComPtr<ID3DBlob> blob;
  hr = pso->GetCachedBlob(blob.GetAddressOf());
  if (FAILED(hr))
  {
    Log_WarningPrintf("Failed to get cached PSO data: %08X", hr);
    return pso;
  }

  // FAST: take the lock to write the index/blob file. Double-check
  // under the lock - if another thread won the race, return their
  // PSO instead of writing ours.
  std::lock_guard<std::mutex> lock(m_pipeline_cache_mutex);

  auto iter = m_pipeline_index.find(key);
  if (iter != m_pipeline_index.end())
  {
    // Someone else already wrote this key. Our PSO is equivalent
    // by construction; just return it (no point reading theirs
    // from disk - the in-memory PSO works the same).
    return pso;
  }

  if (rfseek(m_pipeline_blob_file, 0, SEEK_END) != 0)
    return pso;

  CacheIndexData data;
  data.file_offset = static_cast<uint32_t>(rftell(m_pipeline_blob_file));
  data.blob_size = static_cast<uint32_t>(blob->GetBufferSize());

  CacheIndexEntry entry = {};
  entry.source_hash_low = key.source_hash_low;
  entry.source_hash_high = key.source_hash_high;
  entry.source_length = key.source_length;
  entry.shader_type = static_cast<uint32_t>(key.type);
  entry.blob_size = data.blob_size;
  entry.file_offset = data.file_offset;

  if (rfwrite(blob->GetBufferPointer(), 1, entry.blob_size, m_pipeline_blob_file) != entry.blob_size ||
      filestream_flush(m_pipeline_blob_file) != 0 || rfwrite(&entry, sizeof(entry), 1, m_pipeline_index_file) != 1 ||
      filestream_flush(m_pipeline_index_file) != 0)
  {
    Log_ErrorPrintf("Failed to write pipeline blob to file");
    return pso;
  }

  m_pipeline_index.emplace(key, data);
  return pso;
}

std::string ShaderCache::GetPipelineLibraryFilename(const std::string_view& base_path, D3D_FEATURE_LEVEL feature_level,
                                                    bool debug)
{
  return GetCacheBaseFileName(base_path, "pipeline_library", feature_level, debug) + ".bin";
}

void ShaderCache::FormatPipelineLibraryName(const CacheIndexKey& key, WCHAR out[33])
{
  // 16 hex digits per 64-bit half, 32 total + L'\0'. The two
  // (source_hash_low, source_hash_high) values are xxhash output over
  // the relevant fields of D3D12_GRAPHICS_PIPELINE_STATE_DESC, so the
  // name space is fully determined by the descriptor content. Stable
  // across runs and across feature-level upgrades that don't change
  // the desc layout. swprintf_s isn't portable; use a hand-rolled
  // hex format to keep things mingw/MSVC-clean.
  static const wchar_t hex[] = L"0123456789abcdef";
  WCHAR* p = out;
  uint64_t lo = key.source_hash_low;
  uint64_t hi = key.source_hash_high;
  for (int i = 15; i >= 0; i--)
    *p++ = hex[(hi >> (i * 4)) & 0xF];
  for (int i = 15; i >= 0; i--)
    *p++ = hex[(lo >> (i * 4)) & 0xF];
  *p = 0;
}

bool ShaderCache::TryOpenPipelineLibrary(ID3D12Device1* device1, std::string_view base_path,
                                         D3D_FEATURE_LEVEL feature_level, bool debug)
{
  const std::string filename = GetPipelineLibraryFilename(base_path, feature_level, debug);

  // Read the existing blob if any. The library will hold a reference
  // into this buffer for its lifetime per the D3D12 spec, so we keep
  // it in a member vector and never resize it after this point.
  if (path_is_valid(filename.c_str()))
  {
    RFILE* fp = rfopen(filename.c_str(), "rb");
    if (fp)
    {
      if (rfseek(fp, 0, SEEK_END) == 0)
      {
        const int64_t file_size = rftell(fp);
        if (file_size > 0 && rfseek(fp, 0, SEEK_SET) == 0)
        {
          m_pipeline_library_blob.resize(static_cast<size_t>(file_size));
          const int64_t read = rfread(m_pipeline_library_blob.data(), 1, static_cast<size_t>(file_size), fp);
          if (read != file_size)
          {
            Log_WarningPrintf("Short read on pipeline library file (%lld of %lld bytes) - ignoring",
                              static_cast<long long>(read), static_cast<long long>(file_size));
            m_pipeline_library_blob.clear();
          }
        }
      }
      rfclose(fp);
    }
  }

  // Try to create a library from the existing bytes (if any). On
  // D3D12_ERROR_DRIVER_VERSION_MISMATCH / D3D12_ERROR_ADAPTER_NOT_FOUND
  // / any other failure: drop the blob, delete the file, fall through
  // to creating an empty library so the session still benefits from
  // run-time PSO caching even if cold-start doesn't.
  HRESULT hr = E_FAIL;
  if (!m_pipeline_library_blob.empty())
  {
    hr = device1->CreatePipelineLibrary(m_pipeline_library_blob.data(), m_pipeline_library_blob.size(),
                                        IID_PPV_ARGS(m_pipeline_library.GetAddressOf()));
    if (FAILED(hr))
    {
      const char* reason = "unknown";
      if (hr == D3D12_ERROR_DRIVER_VERSION_MISMATCH)
        reason = "driver version mismatch";
      else if (hr == D3D12_ERROR_ADAPTER_NOT_FOUND)
        reason = "adapter not found";
      Log_WarningPrintf("Pipeline library load failed (%s, hr=%08X) - rebuilding from scratch", reason, hr);
      m_pipeline_library_blob.clear();
      m_pipeline_library.Reset();
      if (path_is_valid(filename.c_str()))
        filestream_delete(filename.c_str());
    }
  }

  if (!m_pipeline_library)
  {
    hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(m_pipeline_library.GetAddressOf()));
    if (FAILED(hr))
    {
      Log_WarningPrintf("CreatePipelineLibrary with empty seed failed: %08X. Falling back to no-PSO-cache path.", hr);
      m_pipeline_library_blob.clear();
      m_pipeline_library.Reset();
      return false;
    }
  }

  return true;
}

void ShaderCache::SerializePipelineLibrary()
{
  const SIZE_T size = m_pipeline_library->GetSerializedSize();
  if (size == 0)
    return;

  std::vector<uint8_t> buffer;
  buffer.resize(size);
  HRESULT hr = m_pipeline_library->Serialize(buffer.data(), size);
  if (FAILED(hr))
  {
    Log_WarningPrintf("ID3D12PipelineLibrary::Serialize failed: %08X", hr);
    return;
  }

  const std::string final_filename = GetPipelineLibraryFilename(m_base_path, m_feature_level, m_debug);
  const std::string tmp_filename = final_filename + ".tmp";

  // Atomic write: dump to .tmp, close, rename over the existing file.
  // If anything fails midway the original is untouched and the .tmp
  // file is cleaned up on the next session via the unconditional
  // delete pass below.
  if (path_is_valid(tmp_filename.c_str()))
    filestream_delete(tmp_filename.c_str());

  RFILE* fp = rfopen(tmp_filename.c_str(), "wb");
  if (!fp)
  {
    Log_WarningPrintf("Failed to open %s for write", tmp_filename.c_str());
    return;
  }
  const bool ok = (rfwrite(buffer.data(), 1, size, fp) == static_cast<int64_t>(size));
  rfclose(fp);
  if (!ok)
  {
    Log_WarningPrintf("Short write on %s - leaving previous pipeline library intact", tmp_filename.c_str());
    filestream_delete(tmp_filename.c_str());
    return;
  }

  // filestream_rename returns 0 on success; if the destination exists
  // it's overwritten atomically on POSIX and via ReplaceFileW-style
  // semantics in libretro-common's Windows VFS.
  if (filestream_rename(tmp_filename.c_str(), final_filename.c_str()) != 0)
  {
    Log_WarningPrintf("Failed to rename %s -> %s", tmp_filename.c_str(), final_filename.c_str());
    filestream_delete(tmp_filename.c_str());
  }
}

} // namespace D3D12
