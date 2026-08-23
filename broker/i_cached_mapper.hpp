#pragma once

// broker/i_cached_mapper.hpp — alias include for DIP stability (SPEC §11.2.1 Q33).
//
// IMapper + ICachedMapper are declared together in i_mapper.hpp (single header
// for include simplicity, ISP-segregated by inheritance). This header is an
// alias so app/ DIP graph stays stable whether it includes i_mapper or
// i_cached_mapper.

#include "broker/i_mapper.hpp"
