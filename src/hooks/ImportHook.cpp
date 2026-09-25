#include "hooks/ImportHook.h"

#include <cstring>

namespace vcontroller {

namespace {

template <typename T>
T* rva(HMODULE module, DWORD offset) {
    return reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(module) + offset);
}

bool matches(HMODULE module, const IMAGE_THUNK_DATA& nameThunk, ImportRef function) {
    if (IMAGE_SNAP_BY_ORDINAL(nameThunk.u1.Ordinal)) {
        return function.ordinal != 0 && IMAGE_ORDINAL(nameThunk.u1.Ordinal) == function.ordinal;
    }
    if (function.name == nullptr) {
        return false;
    }
    const auto* byName =
        rva<IMAGE_IMPORT_BY_NAME>(module, static_cast<DWORD>(nameThunk.u1.AddressOfData));
    return std::strcmp(reinterpret_cast<const char*>(byName->Name), function.name) == 0;
}

} // namespace

void* patchImport(HMODULE module, const char* dllName, ImportRef function, void* replacement) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto* nt = rva<const IMAGE_NT_HEADERS>(module, static_cast<DWORD>(dos->e_lfanew));
    const IMAGE_DATA_DIRECTORY& importDir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress == 0) {
        return nullptr;
    }

    for (auto* desc = rva<IMAGE_IMPORT_DESCRIPTOR>(module, importDir.VirtualAddress);
         desc->Name != 0; ++desc) {
        if (_stricmp(rva<const char>(module, desc->Name), dllName) != 0) {
            continue;
        }
        // OriginalFirstThunk (the name/ordinal table) is what identifies
        // each slot; FirstThunk (the IAT) is what the game actually calls
        // through, and what we overwrite.
        if (desc->OriginalFirstThunk == 0) {
            continue; // no name table to match against (rare, old linkers)
        }
        auto* nameThunk = rva<IMAGE_THUNK_DATA>(module, desc->OriginalFirstThunk);
        auto* addressThunk = rva<IMAGE_THUNK_DATA>(module, desc->FirstThunk);
        for (; nameThunk->u1.AddressOfData != 0; ++nameThunk, ++addressThunk) {
            if (!matches(module, *nameThunk, function)) {
                continue;
            }
            void** slot = reinterpret_cast<void**>(&addressThunk->u1.Function);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
                return nullptr;
            }
            void* original = *slot;
            *slot = replacement;
            VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
            return original;
        }
    }
    return nullptr;
}

} // namespace vcontroller
