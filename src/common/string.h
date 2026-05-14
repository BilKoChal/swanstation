#pragma once
#include "types.h"
#include <cstdarg>
#include <cstring>
#include <string>
#include <string_view>

//
// String
// Implements a UTF-8 string container with copy-on-write behavior.
// The data class is not currently threadsafe (creating a mutex on each container would be overkill),
// so locking is still required when multiple threads are involved.
//
class String
{
public:
  // Internal StringData class.
  struct StringData
  {
    // Pointer to memory where the string is located
    char* pBuffer;

    // Length of the string located in pBuffer (in characters)
    u32 StringLength;

    // Size of the buffer pointed to by pBuffer
    u32 BufferSize;

    // Reference count of this data object. If set to -1,
    // it is considered noncopyable and any copies of the string
    // will always create their own copy.
    s32 ReferenceCount;

    // Whether the memory pointed to by pBuffer is writable.
    bool ReadOnly;
  };

public:
  // Creates an empty string.
  String();

  // Creates a string containing the specified text.
  // Note that this will incur a heap allocation, even if Text is on the stack.
  // For strings that do not allocate any space on the heap, see StaticString.
  String(const char* Text);

  // Creates a string contained the specified text (with length).
  String(const char* Text, u32 Count);

  // Creates a string using the same buffer as another string (copy-on-write).
  String(const String& copyString);

  // Move constructor, take reference from other string.
  String(String&& moveString);

  // Construct a string from a data object, does not increment the reference count on the string data, use carefully.
  explicit String(StringData* pStringData) : m_pStringData(pStringData) {}

  // Creates string from string_view.
  String(const std::string_view& sv);

  // Destructor. Child classes may not have any destructors, as this is not virtual.
  ~String();

  // manual assignment
  void Assign(const String& copyString);
  void Assign(const char* copyText);
  void Assign(const std::string& copyString);
  void Assign(const std::string_view& copyString);
  void Assign(String&& moveString);

  // Ensures that the string has its own unique copy of the data.
  void EnsureOwnWritableCopy();

  // Ensures that we have our own copy of the buffer, and spaceRequired bytes free in the buffer.
  void EnsureRemainingSpace(u32 spaceRequired);

  // clears the contents of the string
  void Clear();

  // append a string to this string
  void AppendString(const String& appendStr);
  void AppendString(const char* appendText);
  void AppendString(const char* appendString, u32 Count);
  void AppendString(const std::string& appendString);
  void AppendString(const std::string_view& appendString);

  // set to formatted string
  void Format(const char* FormatString, ...) printflike(2, 3);
  void FormatVA(const char* FormatString, std::va_list ArgPtr);

  // compare one string to another
  bool Compare(const String& otherString) const;
  bool Compare(const char* otherText) const;

  // starts with / ends with
  bool StartsWith(const char* compareString, bool caseSensitive = true) const;
  bool StartsWith(const String& compareString, bool caseSensitive = true) const;
  bool EndsWith(const char* compareString, bool caseSensitive = true) const;
  bool EndsWith(const String& compareString, bool caseSensitive = true) const;

  // Cuts characters off the string to reduce it to len bytes long.
  void Resize(u32 newSize, char fillerCharacter = ' ');

  // updates the internal length counter when the string is externally modified
  void UpdateSize();

  // gets the size of the string
  u32 GetLength() const { return m_pStringData->StringLength; }
  bool IsEmpty() const { return (m_pStringData->StringLength == 0); }

  // gets the maximum number of bytes we can write to the string, currently
  u32 GetBufferSize() const { return m_pStringData->BufferSize; }

  // replaces all instances of string s with string r in this string
  // returns the number of changes
  u32 Replace(const char* searchString, const char* replaceString);

  // gets a constant pointer to the string
  const char* GetCharArray() const { return m_pStringData->pBuffer; }

  // gets a writable char array, do not write more than reserve characters to it.
  char* GetWriteableCharArray()
  {
    EnsureOwnWritableCopy();
    return m_pStringData->pBuffer;
  }

  // creates a new string from the specified format
  static String FromFormat(const char* FormatString, ...) printflike(1, 2);

  // accessor operators
  operator const char*() const { return GetCharArray(); }
  operator char*() { return GetWriteableCharArray(); }
  operator std::string_view() const
  {
    return IsEmpty() ? std::string_view() : std::string_view(GetCharArray(), GetLength());
  }

  // Will use the string data provided.
  String& operator=(const String& copyString)
  {
    Assign(copyString);
    return *this;
  }

  // Allocates own buffer and copies text.
  String& operator=(const char* Text)
  {
    Assign(Text);
    return *this;
  }
  String& operator=(const std::string& Text)
  {
    Assign(Text);
    return *this;
  }
  String& operator=(const std::string_view& Text)
  {
    Assign(Text);
    return *this;
  }

  // Move operator.
  String& operator=(String&& moveString)
  {
    Assign(moveString);
    return *this;
  }

protected:
  // Internal append function.
  void InternalAppend(const char* pString, u32 Length);

  // Pointer to string data.
  StringData* m_pStringData;

  // Empty string data.
  static const StringData s_EmptyStringData;
};

// stack-allocated string
template<u32 L>
class StackString : public String
{
public:
  StackString() : String(&m_sStringData) { InitStackStringData(); }

  StackString(const char* Text) : String(&m_sStringData)
  {
    InitStackStringData();
    Assign(Text);
  }

  StackString(const char* Text, u32 Count) : String(&m_sStringData)
  {
    InitStackStringData();
    AppendString(Text, Count);
  }

  StackString(const String& copyString) : String(&m_sStringData)
  {
    // force a copy by passing it a string pointer, instead of a string object
    InitStackStringData();
    Assign(copyString.GetCharArray());
  }

  StackString(const StackString& copyString) : String(&m_sStringData)
  {
    // force a copy by passing it a string pointer, instead of a string object
    InitStackStringData();
    Assign(copyString.GetCharArray());
  }

  StackString(const std::string_view& sv) : String(&m_sStringData)
  {
    InitStackStringData();
    AppendString(sv.data(), static_cast<u32>(sv.size()));
  }

  // Override the fromstring method
  static StackString FromFormat(const char* FormatString, ...) printflike(1, 2)
  {
    std::va_list argPtr;
    va_start(argPtr, FormatString);

    StackString returnValue;
    returnValue.FormatVA(FormatString, argPtr);

    va_end(argPtr);

    return returnValue;
  }

  // Will use the string data provided.
  StackString& operator=(const StackString& copyString)
  {
    Assign(copyString.GetCharArray());
    return *this;
  }
  StackString& operator=(const String& copyString)
  {
    Assign(copyString.GetCharArray());
    return *this;
  }

  // Allocates own buffer and copies text.
  StackString& operator=(const char* Text)
  {
    Assign(Text);
    return *this;
  }
  StackString& operator=(const std::string& Text)
  {
    Assign(Text);
    return *this;
  }
  StackString& operator=(const std::string_view& Text)
  {
    Assign(Text);
    return *this;
  }

private:
  StringData m_sStringData;
  char m_strStackBuffer[L + 1];

  inline void InitStackStringData()
  {
    m_sStringData.pBuffer = m_strStackBuffer;
    m_sStringData.StringLength = 0;
    m_sStringData.BufferSize = countof(m_strStackBuffer);
    m_sStringData.ReadOnly = false;
    m_sStringData.ReferenceCount = -1;

#ifdef _DEBUG
    std::memset(m_strStackBuffer, 0, sizeof(m_strStackBuffer));
#else
    m_strStackBuffer[0] = '\0';
#endif
  }
};

// stack string types
typedef StackString<64> TinyString;
typedef StackString<256> SmallString;
typedef StackString<512> PathString;
