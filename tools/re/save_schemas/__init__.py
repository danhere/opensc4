"""Save decoders, one module per simulator (tools/re/sc4save.py).

Each module defines SCHEMAS = {class ID: (name, decoder)}; a decoder takes a
common.Reader positioned at the record's start and returns its fields. A new
simulator adds a module here; nothing else needs to change. Two modules that
claim the same class ID are an error.
"""
import importlib
import pkgutil

from .common import Reader, header  # noqa: F401  (re-exported for sc4save)

SCHEMAS = {}
_owner = {}
for _info in sorted(pkgutil.iter_modules(__path__), key=lambda m: m.name):
    if _info.name == 'common':
        continue
    for _clsid, _schema in importlib.import_module('.' + _info.name, __name__).SCHEMAS.items():
        if _clsid in SCHEMAS:
            raise ImportError('save_schemas: class %08X is in both %s and %s' % (_clsid, _owner[_clsid], _info.name))
        SCHEMAS[_clsid], _owner[_clsid] = _schema, _info.name
