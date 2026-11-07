#pragma once
// Minimal DMAP (application/x-dmap-tagged) reader for RAOP SET_PARAMETER metadata.
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

// Flattens every tag found (recursing into known containers: mlit, mlcl, mlog, mdcl, msrv, mccr)
// into tag -> raw value. Strings are UTF-8 as sent.
std::map<std::string, std::string> dmapParse(const uint8_t* data, size_t len);
