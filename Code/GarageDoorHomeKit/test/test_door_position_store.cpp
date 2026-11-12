#include "DoorPositionStore.h"
#include "DoorPositionRestore.h"
#include <Preferences.h>

#include <cassert>
#include <cstdio>

namespace FakePreferences {
std::vector<unsigned char> data;
bool exists = false;
bool failBegin = false;
bool failWrite = false;
bool failRemove = false;
unsigned writes = 0;
}

int main() {
  using State = GarageDoorModel::State;
  using Reset = DoorPositionRestore::Reset;
  using Decision = DoorPositionRestore::Decision;
  const GarageDoorModel::Position position = {
      1, static_cast<uint8_t>(State::Open), static_cast<uint8_t>(State::Opening), 1, 0};

  DoorPositionStore store;
  assert(store.load() && !store.present());
  assert(store.save(position));
  assert(store.save(position));
  assert(FakePreferences::writes == 1);

  DoorPositionStore rebooted;
  assert(rebooted.load() && rebooted.present());
  assert(DoorPositionRestore::decide(Reset::Software, false, rebooted.present(),
                                     rebooted.record()) == Decision::Restore);
  assert(rebooted.clear());
  DoorPositionStore duringMovement;
  assert(duringMovement.load() && !duringMovement.present());
  assert(DoorPositionRestore::decide(Reset::Software, false, duringMovement.present(),
                                     duringMovement.record()) == Decision::NoRecord);

  assert(duringMovement.save(position));
  assert(DoorPositionRestore::decide(Reset::Software, true, duringMovement.present(),
                                     duringMovement.record()) == Decision::ReedClosed);
  assert(duringMovement.clear());
  assert(!FakePreferences::exists);

  FakePreferences::exists = true;
  FakePreferences::data = {1, 2};
  DoorPositionStore invalid;
  assert(invalid.load());
  assert(DoorPositionRestore::decide(Reset::Software, false, invalid.present(),
                                     invalid.record()) == Decision::InvalidRecord);
  assert(invalid.clear());

  FakePreferences::failBegin = true;
  assert(!invalid.load());
  assert(!invalid.save(position));
  FakePreferences::failBegin = false;
  FakePreferences::failWrite = true;
  assert(!invalid.save(position) && !invalid.present());
  FakePreferences::failWrite = false;
  assert(invalid.save(position));
  FakePreferences::failRemove = true;
  assert(!invalid.clear() && invalid.present());
  FakePreferences::failRemove = false;
  assert(invalid.clear());
  std::puts("Door position store tests passed");
}
