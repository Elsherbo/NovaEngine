// ============================================================
// FILE:    engine/renderer/bsp/ibsp_collision.h
// MODULE:  Renderer > BSP
// PHASE:   2
// STATUS:  DONE
// PURPOSE: Minimal interface for collision queries.
//          Exposes only what's needed for physics — no rendering
//          internals (faces, lightmaps, GPU handles).
//
// Note: Struct definitions are in bsp.h. This interface
// provides abstract access to collision geometry. Physics code
// should include bsp.h for full struct definitions.
// ============================================================
#pragma once

#include "engine/entities/entity_id.h"
#include <cstdint>

namespace nova
{

struct BSPPlane;
struct BSPNode;
struct BSPLeaf;
struct BSPBrush;
struct BSPBrushSide;

class IBSPCollisionWorld
{
public:
    virtual ~IBSPCollisionWorld() = default;

    virtual const BSPNode      *nodes()       const = 0;
    virtual const BSPLeaf    *leaves()      const = 0;
    virtual const BSPPlane   *planes()      const = 0;
    virtual const BSPBrush   *brushes()     const = 0;
    virtual const BSPBrushSide *brushSides() const = 0;
    virtual const int         *leafBrushes() const = 0;
    virtual const uint16_t *leafFaces()  const = 0;

    virtual int nodeCount()      const = 0;
    virtual int leafCount()     const = 0;
    virtual int planeCount()    const = 0;
    virtual int brushCount()    const = 0;
    virtual int brushSideCount() const = 0;
    virtual int leafBrushCount() const = 0;
};

} // namespace nova