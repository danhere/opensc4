// Import address table patching: redirects one module's imports of a
// function, leaving every other module (ours included) on the original.
#pragma once

namespace opensc4::iat {

// Points `module`'s import of `dll`!`function` at `replacement` and stores the
// previous target in `*original`. Returns false if the module does not import
// it. `module` null: the process's exe.
bool Patch(void* module, const char* dll, const char* function, void* replacement, void** original);

template <typename Fn>
bool Patch(void* module, const char* dll, const char* function, Fn replacement, Fn* original)
{
    return Patch(module, dll, function, reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(original));
}

} // namespace opensc4::iat
