#pragma once

#include <windows.h>

#include <cstdint>

namespace vcontroller {

/// Identifies one imported function: by name, by ordinal, or both.
/// Wreckfest imports XInput by ordinal only (XInputGetState is ordinal 2),
/// while most other DLLs are imported by name, so a match on either one
/// counts.
struct ImportRef {
    const char* name = nullptr; // nullptr = don't match by name
    std::uint16_t ordinal = 0;  // 0 = don't match by ordinal
};

/// Redirects one of `module`'s import address table (IAT) entries to
/// `replacement`, and returns the original function pointer it held — or
/// nullptr if `module` doesn't import that function from `dllName`
/// (matched case-insensitively), in which case nothing is patched.
///
/// This only affects calls made *from* `module` (here: the game
/// executable). It's the least invasive hook there is — no code bytes
/// are rewritten, just one pointer in a table the loader already filled
/// in — which also makes it behave identically under Wine/Proton and on
/// Windows.
void* patchImport(HMODULE module, const char* dllName, ImportRef function, void* replacement);

} // namespace vcontroller
