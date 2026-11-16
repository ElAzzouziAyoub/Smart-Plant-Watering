#pragma once
#include <Arduino.h>
#include "Model.h"

// A plain (extended) finite-state machine:
//   - a finite set of states, numbered 0 .. MAX_STATES-1 (use an enum),
//   - transitions   from --[guard] / action--> to.
//
// The automaton itself has NO clocks and NO continuous dynamics.
// Anything about time ("the pump has run for 10 s") or about measurements
// ("moisture <= 40 %") is computed inside the guard functions, which the
// automaton simply calls.
//
// Lookup: states are small numbers, so an array indexed by the state works
// as a perfect hash map (O(1), no hashing, no dynamic memory - std::unordered_map
// is not available on AVR Arduinos anyway). For each state we keep a linked
// list (through array indices) of the transitions leaving it, so run() only
// looks at the transitions of the CURRENT state.
class Automaton : public Model {
public:
  typedef bool (*Guard)();   // condition: may this transition be taken now?
  typedef void (*Action)();  // runs once, at the moment the transition is taken
  struct Transition { byte from, to; Guard guard; Action action; };

  static const byte MAX_STATES = 5;       // state ids must be < MAX_STATES
  static const byte MAX_TRANSITIONS = 14;  // temperature automaton uses 12

  Automaton();
  bool addState(byte id);
  // Transitions leaving the same state are tried in the order they are added,
  // so add the most important one (e.g. a safety transition) first.
  bool addTransition(byte from, byte to, Guard guard, Action action);
  void setInitial(byte id);
  byte state() const;

  void init() override;  // go to the initial state
  void run() override;   // take AT MOST ONE enabled transition

private:
  static const byte NONE = 0xFF;          // "no transition" marker

  Transition transitions_[MAX_TRANSITIONS];
  byte next_[MAX_TRANSITIONS];  // next transition with the same 'from' (or NONE)
  byte first_[MAX_STATES];      // first transition leaving each state (or NONE)
  byte last_[MAX_STATES];       // last one, to append in O(1) and keep the order
  bool exists_[MAX_STATES];     // was the state added?
  byte transitionCount_, initial_, current_;

  bool hasState(byte id) const { return id < MAX_STATES && exists_[id]; }
};
