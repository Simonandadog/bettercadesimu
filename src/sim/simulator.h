#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>

#include "cad/cadfile.h"

namespace sim {

// Result of one simulation pass. All keys are object IDs.
struct State
{
    QHash<int, bool> liveLines;
    QHash<int, bool> activeLoads;   // coils, lamps, motors, timers, resistors
    QHash<int, bool> closedContacts; // fuses, breakers and every contact
    QSet<int> highlighted;          // wires and devices to draw as active
    QStringList warnings;
    bool hasSource = false;
};

// A small relay-logic simulator: it builds a connectivity graph from the
// placed wires and pins, energises loads between a live and a return rail, and
// lets contactor/relay coils close their label-matched contacts.
class Simulator
{
public:
    void setObjects(const QList<cad::Object>& objects);
    const QList<cad::Object>& objects() const { return m_objects; }

    // Solves the circuit. Coil/relay state is remembered between calls so that
    // a seal-in contact keeps a contactor latched after its start button is
    // released.
    State compute();

    // Push buttons and manual switches can be operated while simulating.
    bool isToggleable(int objectId) const;
    bool manualState(int objectId) const;
    void setManualState(int objectId, bool operated);
    void toggle(int objectId);
    void resetManual();

    // Clears both the switch positions and the relay (coil) state.
    void reset();

private:
    QList<cad::Object> m_objects;
    QHash<int, bool> m_manual;
    QHash<QString, bool> m_coilStates;
};

} // namespace sim
