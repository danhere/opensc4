"""Save decoders of the simulator, the clock and scheduler
(tools/re/sc4save.py). SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def simulator(r):
    """cSC4Simulator (0x2990C1E5): Write at 0x00702290 (version 4), Read at
    0x00702340 (versions up to 4; before 4 a stopwatch follows the agent
    types). Names give the object offsets; the cIGZSerializable part sits at
    +0x4 (docs/simulator.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 4:
        raise struct.error('version %d not known' % v)
    out['u2d'], out['established_2e'] = r.u8(), r.u8()
    out['paused_30'], out['speed_3c'], out['time_40'] = r.s32(), r.s32(), r.s32()
    out['ms_per_tick_78'] = r.u32()
    out['agent_types'] = [[r.u32(), r.u8()] for _ in range(11)]  # data (+0xB0), unit passed (+0xDC)
    out['day_ec'] = r.u32()
    return out


SCHEMAS = {0x2990C1E5: ('cSC4Simulator', simulator)}
