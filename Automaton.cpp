#include "Automaton.h"

Automaton::Automaton() : transitionCount_(0), initial_(0), current_(0) {
  for (byte s = 0; s < MAX_STATES; ++s) {
    first_[s] = last_[s] = NONE;
    exists_[s] = false;
  }
}

// Returns false if the id is too big or the state already exists.
bool Automaton::addState(byte id) {
  if (id >= MAX_STATES || exists_[id]) return false;
  exists_[id] = true;
  return true;
}

// Returns false if the table is full or a state was never added.
// The new transition is appended to the list of its 'from' state,
// so the order of addTransition() calls is the priority order.
bool Automaton::addTransition(byte from, byte to, Guard guard, Action action) {
  if (transitionCount_ >= MAX_TRANSITIONS || !hasState(from) || !hasState(to))
    return false;
  byte t = transitionCount_++;
  transitions_[t] = {from, to, guard, action};
  next_[t] = NONE;
  if (first_[from] == NONE) first_[from] = t;   // first transition of this state
  else next_[last_[from]] = t;                  // link after the previous one
  last_[from] = t;
  return true;
}

// Ignored if the state was never added (current_ is used as an array index).
void Automaton::setInitial(byte id) { if (hasState(id)) initial_ = id; }
byte Automaton::state() const { return current_; }

void Automaton::init() { current_ = initial_; }

void Automaton::run() {
  // O(1) jump to the transitions of the current state, then try them in order.
  for (byte t = first_[current_]; t != NONE; t = next_[t]) {
    Transition& tr = transitions_[t];
    if (!tr.guard || tr.guard()) {       // no guard = always enabled
      if (tr.action) tr.action();
      current_ = tr.to;
      return;                            // one transition per run()
    }
  }
  // No enabled transition: stay in the current state.
}
