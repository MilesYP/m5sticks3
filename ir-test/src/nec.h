#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nec {

struct Symbol {
  uint16_t firstUs;
  uint16_t secondUs;
  bool firstHigh;
  bool secondHigh;
};

enum class Kind { Unknown, Standard, Extended, Repeat };

struct Frame {
  constexpr Frame(Kind kind = Kind::Unknown, uint16_t address = 0,
                  uint8_t command = 0, uint32_t raw = 0)
      : kind(kind), address(address), command(command), raw(raw) {}

  Kind kind;
  uint16_t address;
  uint8_t command;
  uint32_t raw;
};

constexpr size_t kFullFrameSymbols = 34;
Frame decode(const Symbol* symbols, size_t count);
const char* name(Kind kind);

}  // namespace nec
