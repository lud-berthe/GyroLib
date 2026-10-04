# Versioning

[Documentation](INDEX.md) / Maintenance

GyroLib **1.0.0** establishes the first stable public contract. Library versions identify releases; ABI
versions identify binary contracts, and the INI schema identifies the file format.
These numbers serve different purposes and do not change with every edit.

## Library releases

| Change | Example | Compatibility |
|---|---|---|
| Compatible bug fix | 1.0.0 → 1.0.1 | Existing integrations and INIs keep working |
| First stable contract | 0.x → 1.0.0 | Stable API and settings baseline |
| Compatible additions after 1.0 | 1.0.x → 1.1.0 | Existing consumers keep working |
| Breaking integration change after 1.0 | 1.x → 2.0.0 | Migration instructions required |

An incompatible struct layout, calling convention, existing signature or documented
semantic change requires a major release after 1.0. Adding an export can require
a newer DLL for mods using it without breaking old consumers.

[version.h](../include/gyrolib/version.h) defines `GL_VERSION_MAJOR`,
`GL_VERSION_MINOR`, `GL_VERSION_PATCH` and `GL_VERSION_STRING`. CMake reads and
checks them. Package compatibility is same-minor during 0.x and same-major after
1.0. Published tags use `vMAJOR.MINOR.PATCH`; unreleased snapshots are identified
by commit rather than presented as unchanged published binaries.

## Settings format

The only supported baseline is:

```ini
schema=0.2.0
```

Version 1.0.0 retains the 0.2.0 format so existing development settings still load.
This format is now part of the published compatibility baseline.

`GL_SETTINGS_SCHEMA` records the release that introduced the format, not necessarily
the installed DLL version. A compatible 1.0.1 fix retains this schema. An optional
key with a safe default does not automatically change it; incompatible structure,
units or enum interpretation does. Use the introducing release's version for
the next format ID.

IDs are three unsigned decimal components, without leading zeros, suffixes or
build metadata, compared component by component. Missing/malformed IDs and
unsupported older formats return `GL_INVALID`; newer formats return
`GL_NEWER_SCHEMA`. Loading/saving does not overwrite unsupported files, and failed
initialization disables autosave.

The discarded unpublished integer formats 1–18 are not migrated. Once a format
is publicly distributed, releases changing it must supply and test migration.
Unknown keys and unregistered saved profiles are retained within the supported
format. [Settings](SETTINGS.md) and [inheritance](INHERITANCE.md#persistence)
describe storage behavior.

## Binary contracts

`GL_ABI_VERSION` and `GL_OVERLAY_ABI_VERSION`, currently 1, identify their binary
contracts separately from releases and INI formats. Compatible added functions
do not change them. Existing layouts/signatures remain valid, but a mod calling
a new symbol still needs a DLL exporting it. Handles must be created and destroyed
by the same loaded library; C++ container/allocator ownership does not cross the C API.

Before publishing, update the appropriate version, document consumer-visible
changes, check package compatibility and run the relevant DLL/static/core and
installed-consumer suites. Retain published-format fixtures and record hardware
acceptance separately. Publication and the first stable compatibility commitment
remain explicit decisions.
