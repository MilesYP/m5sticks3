#include "nec.h"

namespace {

constexpr size_t kRepeatSymbols = 2;
constexpr unsigned kBits = 32;

bool inRange(uint16_t value, uint16_t min, uint16_t max) {
  return value >= min && value <= max;
}

bool lowHigh(const nec::Symbol& symbol) {
  return !symbol.firstHigh && symbol.secondHigh;
}

bool shortMark(const nec::Symbol& symbol) {
  return !symbol.firstHigh && inRange(symbol.firstUs, 300, 850);
}

bool trailer(const nec::Symbol& symbol) {
  // ESP-IDF 的最後一個 RMT symbol 可以只有停止脈衝，第二段長度為零。
  return shortMark(symbol) && (symbol.secondUs == 0 || symbol.secondHigh);
}

}  // namespace

namespace nec {

Frame decode(const Symbol* symbols, size_t count) {
  if (count != kFullFrameSymbols && count != kRepeatSymbols) return {};
  const Symbol& lead = symbols[0];
  if (!lowHigh(lead) || !inRange(lead.firstUs, 7500, 10500) ||
      !trailer(symbols[count - 1])) return {};

  if (count == kRepeatSymbols) {
    if (inRange(lead.secondUs, 1700, 2900)) return {Kind::Repeat};
    return {};
  }
  if (!inRange(lead.secondUs, 3500, 5500)) return {};

  uint32_t raw = 0;
  for (unsigned bit = 0; bit < kBits; ++bit) {
    const Symbol& symbol = symbols[bit + 1];
    if (!lowHigh(symbol) || !shortMark(symbol)) return {};
    if (inRange(symbol.secondUs, 1200, 2200)) {
      raw |= uint32_t{1} << bit;
    } else if (!inRange(symbol.secondUs, 300, 850)) {
      return {};
    }
  }

  const auto addressLo = static_cast<uint8_t>(raw);
  const auto addressHi = static_cast<uint8_t>(raw >> 8);
  const auto command = static_cast<uint8_t>(raw >> 16);
  const auto commandInverse = static_cast<uint8_t>(raw >> 24);
  if ((command ^ commandInverse) != 0xFF) return {};

  if ((addressLo ^ addressHi) == 0xFF) return {Kind::Standard, addressLo, command, raw};
  return {Kind::Extended, static_cast<uint16_t>(raw), command, raw};
}

const char* name(Kind kind) {
  return kind == Kind::Extended ? "NEC EXT" : "NEC";
}

}  // namespace nec
