# audiod — the Fidelio audio arbiter

The one process that owns the iPhone's audio hardware. Spotify (cspot) and
AirPlay (fidelio-raop) stream PCM to it over a Unix socket; audiod plays the
most-recently-active source (debounced) so switching needs no hardware route
handoff. See **DESIGN.md** for the full design and **proto.h** for the wire
contract. Implementation lands component-by-component per the build order in
DESIGN.md.
