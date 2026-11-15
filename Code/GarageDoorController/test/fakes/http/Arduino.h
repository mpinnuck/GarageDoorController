#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#define PROGMEM

class String {
public:
  String() = default;
  String(const char *s) : value(s) {}
  String(int n) : value(std::to_string(n)) {}
  String(unsigned int n) : value(std::to_string(n)) {}
  const char *c_str() const { return value.c_str(); }
  void reserve(size_t n) { value.reserve(n); }
  String &operator+=(const char *s) { value += s; return *this; }
  String &operator+=(char c) { value += c; return *this; }
  String &operator+=(int n) { value += std::to_string(n); return *this; }
  String &operator+=(unsigned int n) { value += std::to_string(n); return *this; }
  String &operator+=(const String &s) { value += s.value; return *this; }
  bool operator==(const char *s) const { return value == s; }
  bool operator!=(const char *s) const { return value != s; }
private:
  std::string value;
};

inline uint32_t millis() { return 0; }
struct FakeESP {
  unsigned int getFreeHeap() const { return 0; }
  unsigned int getMinFreeHeap() const { return 0; }
};
extern FakeESP ESP;
