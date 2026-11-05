// SPDX-License-Identifier: GPL-3.0-or-later
// Framing and user-file operation codes adapted from the public Surge webview
// bridge at8def5147f58d4b22dfee91b8aa72611412339273. See JUCE-PORT-NOTICE.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

// All messages use clap.webview/3. Numeric fields are explicitly specified
// little-endian primitives, never native SequenceData/GUI object layouts.
namespace Stochas::Wclap::Protocol {
constexpr uint32_t version = 1;
constexpr size_t headerSize = 4;
constexpr size_t maxFrameBytes = 64 * 1024 * 1024;
constexpr uint32_t maxDeltaOperations = 8192;
constexpr uint32_t maxCommands = 256;
constexpr uint32_t maxTelemetryBytes = 32768;
enum Type : uint8_t {
   Hello = 1, Param = 2, Gesture = 3, State = 4, Command = 5,
   Resize = 8, Delta = 10, Poll = 13, Preferences = 14, ReleaseAuditions = 15,
   Snapshot = 64, Telemetry = 65, OutputState = 67, Scale = 68,
   NeedState = 69, Acknowledged = 72,
   CommandAcknowledged = 73,
};
// Delta payload: u64 expected revision,u64 next revision,u32 operation count,
// operations. A transaction is validated completely before prepared publication.
// Acknowledged:u64 revision; NeedState:u64 actual revision. Snapshots hash only
// at load/resync, never on a cell drag or process callback.
enum Operation : uint8_t {
   SetGlobal = 1, SetLayer = 2, SetCell = 3, SetNotes = 4,
   ReplaceChainColumn = 5, SetLayerName = 6, SetPatternName = 7,
   ReplaceMappings = 8, SetScale = 9, SetGroove = 10,
};
class Writer {
   std::vector<uint8_t> *prepared=nullptr;
   bool okay=true;
public:
   explicit Writer(Type type, size_t reserve = 0) {
      bytes.reserve(headerSize + reserve); bytes.push_back(type); bytes.insert(bytes.end(), 3, 0);
   }
   // Owner-thread messages borrow storage allocated at plugin preparation.
   // Return its capacity even when the send fails; never grow it while owned.
   Writer(Type type,std::vector<uint8_t> &storage):prepared(&storage),bytes(std::move(storage)) {
      bytes.clear();
      if(bytes.capacity()<headerSize){okay=false;return;}
      bytes.push_back(type);bytes.insert(bytes.end(),3,0);
   }
   Writer(const Writer&)=delete;
   Writer& operator=(const Writer&)=delete;
   ~Writer(){if(prepared)prepared->swap(bytes);}
   bool valid()const{return okay;}
   template <typename T> Writer &put(const T &value) {
      static_assert(std::is_arithmetic<T>::value, "wire values must be explicit primitives");
      return append(&value,sizeof(T));
   }
   Writer &append(const void *data, size_t size) {
      if(!okay||size>maxFrameBytes-bytes.size()||
         (prepared&&size>bytes.capacity()-bytes.size())){okay=false;return *this;}
      if (size) { const auto *raw = static_cast<const uint8_t*>(data); bytes.insert(bytes.end(), raw, raw + size); }
      return *this;
   }
   std::vector<uint8_t> bytes;
};
class Reader {
   const uint8_t *bytes;
   size_t size, at;
public:
   Reader(const void *data, size_t length) : bytes(static_cast<const uint8_t*>(data)), size(length), at(length < headerSize ? length : headerSize) {}
   bool valid() const {
      return bytes && size >= headerSize && size <= maxFrameBytes && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
   }
   Type type() const { return valid() ? Type(bytes[0]) : Type(0); }
   bool getBytes(size_t count, const uint8_t *&data) {
      if (!valid() || count > size - at) return false;
      data = bytes + at; at += count; return true;
   }
   template <typename T> bool get(T &value) {
      static_assert(std::is_arithmetic<T>::value, "wire values must be explicit primitives");
      const uint8_t *raw = nullptr;
      if (!getBytes(sizeof(T), raw)) return false;
      std::memcpy(&value, raw, sizeof(T)); return true;
   }
   const uint8_t *rest() const { return valid() ? bytes + at : nullptr; }
   size_t remaining() const { return valid() ? size - at : 0; }
};
} // namespace Stochas::Wclap::Protocol
