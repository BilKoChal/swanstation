#include "shader_cache.h"
#include "../file_system.h"
#include "../log.h"
#include "../md5_digest.h"
#include <cstring>

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

// On-disk index record for the (disabled) D3D12 per-PSO pipeline blob
// cache. This used to be byte-for-byte shared with the D3D11 backend's
// bytecode cache, but that cache - and the whole D3D11::ShaderCache
// class - has been removed (every shader is pre-baked now), so there
// is no longer a cross-backend file to stay in sync with. The struct
// is kept only for the per-PSO pipelines cache's on-disk format; the
// 32-byte size assert guards that format's stability across builds.
// shader_type stores EntryType cast to uint32_t.
static_assert(sizeof(CacheIndexEntry) == 32, "CacheIndexEntry must stay 32 bytes for on-disk pipeline-cache format stability");

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
// The shader bytecode cache that this rationale used to point to as
// "the expensive recompile to save" is itself gone now: every D3D12
// shader is pre-baked DXBC (D3DCommon::EmbeddedShaders), so there is no
// runtime HLSL compile to cache and Open() scrubs the orphaned
// d3d_shaders_<sm> files rather than creating them. Driver-side PSO
// assembly from already-compiled DXBC is fast - sub-millisecond on
// modern desktop GPUs - so re-doing it each cold start is not a real
// cost, and the per-PSO blob cache stays disabled. The one cache that
// remains worthwhile is the ID3D12PipelineLibrary (opened separately
// below): a single compact driver-managed file that dedups PSO state
// internally, which is why CanUsePipelineCache (the per-PSO blob path)
// returns false while the pipeline library path is still used.
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
}

bool ShaderCache::CacheIndexKey::operator==(const CacheIndexKey& key) const
{
  return (source_hash_low == key.source_hash_low && source_hash_high == key.source_hash_high &&
          source_length == key.source_length && type == key.type);
}

void ShaderCache::Open(std::string_view base_path, ID3D12Device* device, D3D_FEATURE_LEVEL feature_level,
                       uint32_t version, bool debug)
{
  m_base_path = base_path;
  m_feature_level = feature_level;
  m_version = version;
  m_debug = debug;

  if (!base_path.empty())
  {
    // The shader bytecode cache is gone: every D3D12 shader is
    // pre-baked DXBC consumed from D3DCommon::EmbeddedShaders, so
    // nothing compiles HLSL or calls GetShaderBlob any more, and the
    // D3D11 backend already stopped opening this file. Rather than
    // create an empty d3d_shaders_<sm>.{idx,bin} every launch, scrub
    // the orphaned pair (the unified bytecode cache this backend used
    // to write, shared with the now-removed D3D11 cache). Same cheap
    // path_is_valid+unlink self-healing pattern as the pipeline / legacy
    // scrubs below. The pipeline-library cache (opened further down) is
    // unaffected - it is what makes PSO creation cheap and stays.
    {
      const std::string base_shader_filename = GetCacheBaseFileName(base_path, "shaders", feature_level, debug);
      const std::string shader_index_filename = base_shader_filename + ".idx";
      const std::string shader_blob_filename = base_shader_filename + ".bin";
      if (path_is_valid(shader_index_filename.c_str()))
        filestream_delete(shader_index_filename.c_str());
      if (path_is_valid(shader_blob_filename.c_str()))
        filestream_delete(shader_blob_filename.c_str());
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

    // Scrub the pre-unification per-backend shader bytecode cache
    // (d3d12_shaders_<sm>.{idx,bin}). The shaders cache now lives at
    // the backend-neutral d3d_shaders_<sm>.* shared with the D3D11
    // backend, so the old d3d12_-prefixed file is orphaned. Same
    // self-healing rationale as the pipelines scrub above - cheap
    // path_is_valid+unlink on every Open, removes ~900 KB of dead
    // cache across the one-time upgrade. Built from the literal
    // legacy prefix rather than GetCacheBaseFileName("shaders") since
    // the latter now returns the new neutral name.
    {
      std::string legacy_shaders(base_path);
      legacy_shaders += "d3d12_shaders_";
      switch (feature_level)
      {
        case D3D_FEATURE_LEVEL_10_0: legacy_shaders += "sm40"; break;
        case D3D_FEATURE_LEVEL_10_1: legacy_shaders += "sm41"; break;
        case D3D_FEATURE_LEVEL_11_0: legacy_shaders += "sm50"; break;
        default:                     legacy_shaders += "unk";  break;
      }
      if (debug)
        legacy_shaders += "_debug";
      const std::string legacy_index = legacy_shaders + ".idx";
      const std::string legacy_blob = legacy_shaders + ".bin";
      if (path_is_valid(legacy_index.c_str()))
        filestream_delete(legacy_index.c_str());
      if (path_is_valid(legacy_blob.c_str()))
        filestream_delete(legacy_blob.c_str());
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

  m_open = true;
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
  const uint32_t data_version = m_version;
  if (rfwrite(&index_version, sizeof(index_version), 1, index_file) != 1 ||
      rfwrite(&data_version, sizeof(data_version), 1, index_file) != 1)
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

  uint32_t file_version = 0;
  uint32_t data_version = 0;
  if (rfread(&file_version, sizeof(file_version), 1, index_file) != 1 || file_version != FILE_VERSION ||
      rfread(&data_version, sizeof(data_version), 1, index_file) != 1 || data_version != m_version)
  {
    Log_ErrorPrintf("Bad file/data version in '%s'", index_filename.c_str());
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

  // The shader bytecode cache ("shaders") is API-neutral DXBC keyed
  // by HLSL hash, byte-compatible with the D3D11 backend's cache, so
  // it uses the backend-neutral "d3d_shaders_" prefix and both
  // backends share one file. The pipeline caches ("pipelines",
  // "pipeline_library") are D3D12-only - the disabled per-PSO blob
  // cache and the driver-managed ID3D12PipelineLibrary - so they
  // keep the "d3d12_" prefix.
  if (type == "shaders")
    base_filename += "d3d_shaders_";
  else
  {
    base_filename += "d3d12_";
    base_filename += type;
    base_filename += "_";
  }

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
  // kept correct in case the per-PSO blob cache is ever revived.
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
