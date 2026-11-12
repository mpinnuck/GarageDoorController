#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

namespace FakePreferences {
extern std::vector<unsigned char> data;
extern bool exists;
extern bool failBegin;
extern bool failWrite;
extern bool failRemove;
extern unsigned writes;
}

class Preferences {
public:
  bool begin(const char *, bool) { return !FakePreferences::failBegin; }
  void end() {}
  bool isKey(const char *) { return FakePreferences::exists; }
  size_t getBytesLength(const char *) { return FakePreferences::data.size(); }
  size_t getBytes(const char *, void *out, size_t size) {
    if (size != FakePreferences::data.size()) return 0;
    std::memcpy(out, FakePreferences::data.data(), size);
    return size;
  }
  size_t putBytes(const char *, const void *value, size_t size) {
    if (FakePreferences::failWrite) return 0;
    const auto *bytes = static_cast<const unsigned char *>(value);
    FakePreferences::data.assign(bytes, bytes + size);
    FakePreferences::exists = true;
    ++FakePreferences::writes;
    return size;
  }
  bool remove(const char *) {
    if (FakePreferences::failRemove) return false;
    FakePreferences::exists = false;
    FakePreferences::data.clear();
    return true;
  }
};
