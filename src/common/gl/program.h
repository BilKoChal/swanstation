#pragma once
#include "../types.h"
#include "glad.h"
#include <string_view>
#include <vector>

namespace GL {
class Program
{
public:
  Program();
  Program(const Program&) = delete;
  Program(Program&& prog);
  ~Program();

  static GLuint CompileShader(GLenum type, const std::string_view source);
  static void ResetLastProgram();

  bool Compile(const std::string_view vertex_shader, const std::string_view geometry_shader,
               const std::string_view fragment_shader);

  bool CreateFromBinary(const void* data, uint32_t data_length, uint32_t data_format);

  bool GetBinary(std::vector<uint8_t>* out_data, uint32_t* out_data_format);
  void SetBinaryRetrievableHint();

  void BindAttribute(GLuint index, const char* name);
  void BindDefaultAttributes();

  void BindFragData(GLuint index = 0, const char* name = "o_col0");
  void BindFragDataIndexed(GLuint color_number = 0, const char* name = "o_col0");

  bool Link();

  void Bind() const;

  // Returns true if the program has a non-zero linked GL handle.
  // Used by the lazy-compile path in GPU_HW_OpenGL to distinguish a
  // never-touched matrix slot (program id 0) from one that has
  // already been compiled and is ready for Bind().
  bool IsValid() const { return m_program_id != 0; }

  void Destroy();

  int RegisterUniform(const char* name);
  void Uniform1ui(int index, uint32_t x) const;
  void Uniform2ui(int index, uint32_t x, uint32_t y) const;
  void Uniform3ui(int index, uint32_t x, uint32_t y, uint32_t z) const;
  void Uniform4ui(int index, uint32_t x, uint32_t y, uint32_t z, uint32_t w) const;
  void Uniform1i(int index, int32_t x) const;
  void Uniform2i(int index, int32_t x, int32_t y) const;
  void Uniform3i(int index, int32_t x, int32_t y, int32_t z) const;
  void Uniform4i(int index, int32_t x, int32_t y, int32_t z, int32_t w) const;
  void Uniform1f(int index, float x) const;
  void Uniform2f(int index, float x, float y) const;
  void Uniform3f(int index, float x, float y, float z) const;
  void Uniform4f(int index, float x, float y, float z, float w) const;
  void Uniform2uiv(int index, const uint32_t* v) const;
  void Uniform3uiv(int index, const uint32_t* v) const;
  void Uniform4uiv(int index, const uint32_t* v) const;
  void Uniform2iv(int index, const int32_t* v) const;
  void Uniform3iv(int index, const int32_t* v) const;
  void Uniform4iv(int index, const int32_t* v) const;
  void Uniform2fv(int index, const float* v) const;
  void Uniform3fv(int index, const float* v) const;
  void Uniform4fv(int index, const float* v) const;

  void Uniform1ui(const char* name, uint32_t x) const;
  void Uniform2ui(const char* name, uint32_t x, uint32_t y) const;
  void Uniform3ui(const char* name, uint32_t x, uint32_t y, uint32_t z) const;
  void Uniform4ui(const char* name, uint32_t x, uint32_t y, uint32_t z, uint32_t w) const;
  void Uniform1i(const char* name, int32_t x) const;
  void Uniform2i(const char* name, int32_t x, int32_t y) const;
  void Uniform3i(const char* name, int32_t x, int32_t y, int32_t z) const;
  void Uniform4i(const char* name, int32_t x, int32_t y, int32_t z, int32_t w) const;
  void Uniform1f(const char* name, float x) const;
  void Uniform2f(const char* name, float x, float y) const;
  void Uniform3f(const char* name, float x, float y, float z) const;
  void Uniform4f(const char* name, float x, float y, float z, float w) const;
  void Uniform2uiv(const char* name, const uint32_t* v) const;
  void Uniform3uiv(const char* name, const uint32_t* v) const;
  void Uniform4uiv(const char* name, const uint32_t* v) const;
  void Uniform2iv(const char* name, const int32_t* v) const;
  void Uniform3iv(const char* name, const int32_t* v) const;
  void Uniform4iv(const char* name, const int32_t* v) const;
  void Uniform2fv(const char* name, const float* v) const;
  void Uniform3fv(const char* name, const float* v) const;
  void Uniform4fv(const char* name, const float* v) const;

  void BindUniformBlock(const char* name, uint32_t index);

  Program& operator=(const Program&) = delete;
  Program& operator=(Program&& prog);

private:
  static uint32_t s_last_program_id;

  GLuint m_program_id = 0;
  GLuint m_vertex_shader_id = 0;
  GLuint m_fragment_shader_id = 0;

  std::vector<GLint> m_uniform_locations;
};

} // namespace GL
