"""Save decoders of the city camera (tools/re/sc4save.py). SCHEMAS maps class
IDs to (name, decoder)."""
from .common import header


def camera_control(r):
    """cSC4CameraControl (0xC9C628EC): Write at 0x007CBB10 (version 1): the
    view centre, then the zoom and rotation before and after the last change
    (docs/renderer.md, "The camera control")."""
    out = header(r)
    out['version'] = r.u16()
    out['center'] = [r.f32() for _ in range(3)]
    out['old_zoom'], out['old_rotation'] = r.s32(), r.s32()
    out['zoom'], out['rotation'] = r.s32(), r.s32()
    out['new_zoom'], out['new_rotation'] = r.s32(), r.s32()
    return out


SCHEMAS = {0xC9C628EC: ('cSC4CameraControl', camera_control)}
