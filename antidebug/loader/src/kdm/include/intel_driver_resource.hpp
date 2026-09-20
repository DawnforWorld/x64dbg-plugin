#pragma once
#include <stdint.h>
#include <stddef.h>

// vtdbg STUB SLOT (clause 18a / PROVENANCE.md): the real iqvw64e.sys byte
// array (Intel-proprietary binary, 34,568 bytes upstream) is NEVER committed
// to this repository. Generate the real header into the BUILD tree with:
//   python vt/tools/gen_iqvw64e_header.py <path-to-iqvw64e.sys> -o <build>/gen
// and let the build put that directory first on the include path (wired in
// vt/CMakeLists.txt). This committed stub keeps CI builds compiling with an
// empty array; intel_driver::Load() fails fast when driver_size == 0.
namespace intel_driver_resource
{
	static const uint8_t driver[] = { 0 };
	static const size_t driver_size = 0;
}
