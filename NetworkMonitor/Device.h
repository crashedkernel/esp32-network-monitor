#pragma once
#include <stdint.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <utility>

constexpr unsigned MAX_HOSTS = 255;

// Most aliases and hostnames are short. Allocate only the bytes they use;
// the scanner can share a value between reports until it changes.
template <size_t Limit> class DeviceText {
  struct Allocation { unsigned references; };
  Allocation *value = nullptr;

public:
  DeviceText() = default;
  DeviceText(const DeviceText &other) { *this = other; }
  ~DeviceText() { clear(); }

  DeviceText &operator=(const DeviceText &other) {
    if (value != other.value) {
      clear();
      value = other.value;
      if (value)
        ++value->references;
    }
    return *this;
  }

  const char *c_str() const { return value ? reinterpret_cast<const char *>(value + 1) : ""; }
  bool empty() const { return !value; }
  void clear() {
    if (value && --value->references == 0)
      free(value);
    value = nullptr;
  }

  bool set(const char *text) {
    if (!*text) {
      clear();
      return true;
    }
    const size_t length = strnlen(text, Limit);
    if (strlen(c_str()) == length && !memcmp(c_str(), text, length))
      return true;
    void *memory = malloc(sizeof(Allocation) + length + 1);
    if (!memory)
      return false;
    auto *replacement = new (memory) Allocation{1};
    auto *stored = reinterpret_cast<char *>(replacement + 1);
    memcpy(stored, text, length);
    stored[length] = 0;
    clear();
    value = replacement;
    return true;
  }
};
struct Device {
  uint32_t ip = 0;
  DeviceText<95> name;
  DeviceText<95> hostname;
  char mac[18] = {0};
  bool online = false, changed = false, isNew = false;
  DeviceText<31> connection;
};

// A report grows in small blocks so it can use fragmented heap. Row addresses
// stay stable when another block is allocated.
class DeviceTable {
  static constexpr unsigned BLOCK_SIZE = 16;
  static constexpr unsigned BLOCK_COUNT = (MAX_HOSTS + BLOCK_SIZE - 1) / BLOCK_SIZE;
  Device *blocks[BLOCK_COUNT] = {};
  unsigned count = 0;

public:
  DeviceTable() = default;
  DeviceTable(const DeviceTable &) = delete;
  DeviceTable &operator=(const DeviceTable &) = delete;
  ~DeviceTable() { clear(0); }

  unsigned size() const { return count; }
  Device &operator[](unsigned i) { return blocks[i / BLOCK_SIZE][i % BLOCK_SIZE]; }
  const Device &operator[](unsigned i) const { return blocks[i / BLOCK_SIZE][i % BLOCK_SIZE]; }

  Device *append(const Device &row = Device()) {
    if (count == MAX_HOSTS)
      return nullptr;
    Device *&block = blocks[count / BLOCK_SIZE];
    if (!block)
      block = new (std::nothrow) Device[BLOCK_SIZE];
    if (!block)
      return nullptr;
    Device &slot = (*this)[count++];
    slot = row;
    return &slot;
  }

  // Reuse the blocks needed for a similarly sized report, release surplus.
  void clear(unsigned keep) {
    const unsigned retainedBlocks = (keep + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (unsigned i = 0; i < count && i < retainedBlocks * BLOCK_SIZE; ++i)
      (*this)[i] = Device();
    count = 0;
    for (unsigned i = retainedBlocks; i < BLOCK_COUNT; ++i) {
      delete[] blocks[i];
      blocks[i] = nullptr;
    }
  }

  void swap(DeviceTable &other) {
    std::swap(blocks, other.blocks);
    std::swap(count, other.count);
  }
};
