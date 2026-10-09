"""Save decoders of weather (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
from .common import header


def weather_simulator(r):
    """cSC4WeatherSimulator (0x2990C1BC): Write at 0x00725E80 (version 2).
    The record holds no references, so after the version it is compared
    byte for byte (docs/weather.md)."""
    out = header(r)
    out['version'] = r.u16()
    out['bytes'] = list(r.data[r.pos:])
    r.pos = len(r.data)
    return out


SCHEMAS = {0x2990C1BC: ('cSC4WeatherSimulator', weather_simulator)}
