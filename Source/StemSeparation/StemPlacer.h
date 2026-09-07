#pragma once
#include "Project/Session.h"
#include "StemSeparationService.h"

namespace mashup
{
/** Turns finished separation jobs into sources and tracks: each stem becomes a source (stemOf/stemType set)
    and a new track whose clips mirror the placement of the original source's clips. */
class StemPlacer
{
public:
    explicit StemPlacer (Session&);
    ~StemPlacer();
    void place (const StemSeparationService::Job&);
private:
    Session& session;
};
}
