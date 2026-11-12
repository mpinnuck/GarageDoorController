#include "DoorControlGate.h"

#include <cassert>
#include <cstdio>

static bool stored = false;
static bool saveFails = false;

static bool save(bool enabled) {
  if (saveFails) return false;
  stored = enabled;
  return true;
}

int main() {
  DoorControlGate gate(save);
  assert(!gate.enabled());
  assert(gate.setEnabled(true));
  assert(gate.enabled() && stored);

  DoorControlGate afterRestart(save);
  afterRestart.restore(stored);
  assert(afterRestart.enabled());
  assert(afterRestart.setEnabled(false));
  assert(!afterRestart.enabled() && !stored);

  DoorControlGate disabledRestart(save);
  disabledRestart.restore(stored);
  assert(!disabledRestart.enabled());
  saveFails = true;
  assert(!disabledRestart.setEnabled(true));
  assert(!disabledRestart.enabled());

  afterRestart.restore(true);
  assert(!afterRestart.setEnabled(false));
  assert(!afterRestart.enabled());
  afterRestart.restore(true);
  assert(!afterRestart.setEnabled(true));
  assert(!afterRestart.enabled());
  std::puts("Door control gate tests passed");
}
