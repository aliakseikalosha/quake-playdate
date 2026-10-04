# Models and map formats

[← Documentation index](../README.md)

Everything the game draws and collides with comes from three binary file formats stored in `pak0.pak`:

| Format | Extension | What | Loader |
| --- | --- | --- | --- |
| **BSP** (brush model) | `.bsp` | Levels (`maps/e1m1.bsp`) and doors, platforms, buttons (`*1`, `*2`, … submodels of the level) | `Mod_LoadBrushModel` |
| **MDL** (alias model) | `.mdl` | Animated models: monsters, items, weapons | `Mod_LoadAliasModel` |
| **SPR** (sprite) | `.spr` | Flat billboards: explosions, bubbles | `Mod_LoadSpriteModel` |

| File | Role |
| --- | --- |
| [`bspfile.h`](#bspfileh) | On-disk BSP structures and limits |
| [`modelgen.h`](#modelgenh) | On-disk alias (MDL) structures |
| [`spritegn.h`](#spritegnh) | On-disk sprite (SPR) structures |
| [`model.h`](#modelh) | In-memory structures (`model_t` and friends) |
| [`model.c`](#modelc) | Loading and caching |
| [`anorms.h`](#anormsh) | The 162 precomputed vertex normals |

Naming convention: `d*_t` are **on-disk** structs, `m*_t` are **in-memory** ones.

---

## `bspfile.h`

The BSP file is a header (`dheader_t`: `version` = `BSPVERSION` 29, then 15 `lump_t { fileofs, filelen }` entries) followed by the lumps:

| Lump | Struct | Content |
| --- | --- | --- |
| `LUMP_ENTITIES` | text | The map's entities as `{ "key" "value" }` blocks ([parsed by `ED_LoadFromFile`](quakec.md#pr_edictc)) |
| `LUMP_PLANES` | `dplane_t` | `normal`, `dist`, `type` (`PLANE_X`…`PLANE_ANYZ`) |
| `LUMP_TEXTURES` | `dmiptexlump_t`, `miptex_t` | Textures with 4 mip levels (`MIPLEVELS`) |
| `LUMP_VERTEXES` | `dvertex_t` | Points |
| `LUMP_VISIBILITY` | bytes | Run-length-compressed PVS: which leaves each leaf can see |
| `LUMP_NODES` | `dnode_t` | The BSP tree: `planenum`, two children (negative = `-(leaf+1)`), bounds, faces |
| `LUMP_TEXINFO` | `texinfo_t` | Texture mapping vectors (`vecs[2][4]`) and flags |
| `LUMP_FACES` | `dface_t` | A polygon: first edge, number of edges, plane, side, texinfo, light styles, `lightofs` |
| `LUMP_LIGHTING` | bytes | One byte per 16×16-texel lightmap sample, per light style |
| `LUMP_CLIPNODES` | `dclipnode_t` | The collision hulls (expanded BSP trees) |
| `LUMP_LEAFS` | `dleaf_t` | Convex regions: `contents`, `visofs`, bounds, mark surfaces, ambient sound levels |
| `LUMP_MARKSURFACES` | `short` | Faces of a leaf |
| `LUMP_EDGES` | `dedge_t` | Two vertex numbers (edge 0 is never used) |
| `LUMP_SURFEDGES` | `int` | A face's edges; negative = traversed in reverse |
| `LUMP_MODELS` | `dmodel_t` | Submodels: `mins`, `maxs`, `origin`, `headnode[MAX_MAP_HULLS]`, faces |

Constants worth knowing: `CONTENTS_EMPTY` (-1), `CONTENTS_SOLID` (-2), `CONTENTS_WATER` (-3), `CONTENTS_SLIME`, `CONTENTS_LAVA`, `CONTENTS_SKY`, `CONTENTS_CURRENT_*`;
`MAXLIGHTMAPS` 4 light styles per face; `NUM_AMBIENTS` 4 ambient sounds (water, sky, slime, lava); the `MAX_MAP_*` limits (e.g. `MAX_MAP_LEAFS` 8192, `MAX_MAP_NODES` 32767).

```c
// walking the BSP tree to find which leaf a point is in (model.c)
node = model->nodes;
while (1) {
	if (node->contents < 0) return (mleaf_t *)node;       // a leaf
	plane = node->plane;
	d = DotProduct (p, plane->normal) - plane->dist;
	node = d > 0 ? node->children[0] : node->children[1];  // front or back
}
```

**Change:** the on-disk structs are `__attribute__((packed))` so they match the file layout on any compiler.

## `modelgen.h`

On-disk alias model (`ALIAS_VERSION` 6). The header `mdl_t` has `scale`, `scale_origin`, `boundingradius`, `eyeposition`, `numskins`, `skinwidth`, `skinheight`, `numverts`, `numtris`, `numframes`, `synctype`, `flags`.
After it: skins (single or group), `stvert_t` texture coordinates (`onseam`, `s`, `t`), `dtriangle_t` triangles (`facesfront`, `vertindex[3]`) and frames. Each frame stores `trivertx_t { byte v[3]; byte lightnormalindex; }` per vertex: position quantised to a byte per axis (scaled by
`mdl_t.scale`), and an index into the 162-entry normal table ([`anorms.h`](#anormsh)).

```c
// a vertex in model space
x = v.v[0] * mdl->scale[0] + mdl->scale_origin[0];
```

The header notes it "must be identical in the modelgen directory and in the Quake directory". `ALIAS_ONSEAM` (0x20) marks vertices on the texture seam, which get a half-texture offset on back-facing triangles.

## `spritegn.h`

On-disk sprite: `dsprite_t` (`SPRITE_VERSION` 1: `type`, `boundingradius`, `width`, `height`, `numframes`, `beamlength`, `synctype`), frames (`dspriteframe_t`: `origin`, `width`, `height`) or groups (`dspritegroup_t` plus `dspriteinterval_t`).
Sprite types: `SPR_VP_PARALLEL_UPRIGHT`, `SPR_FACING_UPRIGHT`, `SPR_VP_PARALLEL`, `SPR_ORIENTED`, `SPR_VP_PARALLEL_ORIENTED`.

---

## `model.h`

The in-memory model, one `model_t` per loaded file:

```c
typedef struct model_s {
	char      name[MAX_QPATH];
	qboolean  needload;              // true until Mod_LoadModel has loaded it
	modtype_t type;                  // mod_brush | mod_sprite | mod_alias
	int       numframes;  synctype_t synctype;  int flags;     // flags: EF_ROCKET, EF_GIB, EF_ROTATE, ... trails
	vec3_t    mins, maxs;  float radius;                       // bounds
	/* brush models: */
	int firstmodelsurface, nummodelsurfaces, numsubmodels;  dmodel_t *submodels;
	mplane_t *planes;  mleaf_t *leafs;  mvertex_t *vertexes;  medge_t *edges;  mnode_t *nodes;
	mtexinfo_t *texinfo;  msurface_t *surfaces;  int *surfedges;  dclipnode_t *clipnodes;
	msurface_t **marksurfaces;  hull_t hulls[MAX_MAP_HULLS];  texture_t **textures;
	byte *visdata, *lightdata;  char *entities;
	cache_user_t cache;              // alias and sprite data: only via Mod_Extradata()
} model_t;
```

Key in-memory structs:

| Struct | Meaning |
| --- | --- |
| `mplane_t` | `normal`, `dist`, `type` (axial or not), `signbits` (fast box tests) |
| `texture_t` | A texture: name, size, four mip offsets, animation links (`anim_next`, `alternate_anims`) |
| `mtexinfo_t` | Mapping `vecs[2][4]`, `mipadjust`, `texture`, `flags` |
| `msurface_t` | A face: `plane`, `firstedge`/`numedges`, `texturemins`, `extents`, `samples` (lightmap), `styles`, `cached_light`, `dlightframe`/`dlightbits`, `flags` (`SURF_DRAWSKY`, `SURF_DRAWTURB`, `SURF_PLANEBACK`, …) |
| `mnode_t` / `mleaf_t` | Tree node and leaf (`contents`, `visframe`, `efrags`, `ambient_sound_level`) sharing a common prefix |
| `hull_t` | A collision hull: `clipnodes`, `planes`, `firstclipnode`, `lastclipnode`, `clip_mins`, `clip_maxs` |
| `aliashdr_t`, `maliasframedesc_t`, `mtriangle_t` | Alias model in memory |
| `msprite_t`, `mspriteframe_t`, `mspritegroup_t` | Sprite in memory |

The [Playdate renderer](renderer-pdr.md) builds its own compact copies of the brush data at map load (`pdr_*` structs) and keeps `model_t` as the source.

Public API:

```c
void     Mod_Init (void);
void     Mod_ClearAll (void);                                  // new level: drop everything that is not cached
model_t *Mod_ForName (char *name, qboolean crash);             // find or load
void    *Mod_Extradata (model_t *mod);                         // alias/sprite data, reloading it if evicted
mleaf_t *Mod_PointInLeaf (const vec3_t p, model_t *model);     // BSP descent
byte    *Mod_LeafPVS (mleaf_t *leaf, model_t *model);          // that leaf's visibility set, decompressed
```

---

## `model.c`

| Function | Purpose |
| --- | --- |
| `Mod_ForName(name, crash)` / `Mod_FindName` / `Mod_LoadModel` | Find a slot by name in `mod_known[]` (`MAX_MOD_KNOWN` 256), load the file with `COM_LoadStackFile` / `COM_LoadHunkFile`, dispatch on the 4-byte header (`IDPOLYHEADER`, `IDSPRITEHEADER`, otherwise BSP). |
| `Mod_LoadBrushModel` | Loads each lump in turn: `Mod_LoadVertexes`, `Mod_LoadEdges`, `Mod_LoadSurfedges`, `Mod_LoadTextures`, `Mod_LoadLighting`, `Mod_LoadPlanes`, `Mod_LoadTexinfo`, `Mod_LoadFaces`, `Mod_LoadMarksurfaces`, `Mod_LoadVisibility`, `Mod_LoadLeafs`, `Mod_LoadNodes` (`Mod_SetParent`), `Mod_LoadClipnodes`, `Mod_MakeHull0`, `Mod_LoadEntities`, `Mod_LoadSubmodels`; then splits the file into one `model_t` per submodel. |
| `Mod_LoadTextures` | Reads the mip textures, loads the sky specially (`R_InitSky`), and links animated textures (`+0name`, `+1name` … and alternates `+aname`). |
| `CalcSurfaceExtents(s)` | A face's texture bounds, rounded to 16-texel lightmap cells. |
| `Mod_LoadAliasModel` | Converts an `.mdl` into the cache: header, skins (`Mod_LoadAliasSkin`/`SkinGroup`), texture coordinates, triangles, frames (`Mod_LoadAliasFrame`/`Group`); computes the bounding box. |
| `Mod_LoadSpriteModel`, `Mod_LoadSpriteFrame`, `Mod_LoadSpriteGroup` | Same for sprites. |
| `Mod_DecompressVis`, `Mod_LeafPVS` | Run-length decode: a zero byte is followed by the number of zero bytes it stands for. |
| `Mod_PointInLeaf` | Tree descent. |
| `Mod_TouchModel`, `Mod_Print`, `Mod_ClearAll`, `RadiusFromBounds` | Cache touch, `modellist` listing, reset, bounding radius. |

```c
// loading a model: the first call reads the file, later calls return the cached one
model_t *m = Mod_ForName ("progs/soldier.mdl", true);
aliashdr_t *hdr = Mod_Extradata (m);          // reloads from the pak if the cache evicted it
```

**What this port changed:** `needload` is now a simple boolean instead of the original three-state value (`NL_PRESENT` / `NL_NEEDS_LOADED` / `NL_UNREFERENCED`). Model slots are never recycled: running out of `mod_known` is an error
(`mod_numknown == MAX_MOD_KNOWN`) rather than evicting an unreferenced one. Alias models evicted from the cache are reloaded on demand. `Mod_PointInLeaf` takes a `const vec3_t`; `COM_FileBase` takes an output size;
`Mod_LoadTexinfo` copies the `vecs[2][4]` array element-by-element; maths is single-precision (`floorf`, `ceilf`).

---

## `anorms.h`

The 162 unit vectors used to encode vertex normals in alias models (`lightnormalindex`), as a C initialiser list (`{-0.525731f, 0.000000f, 0.850651f}, …`) included inside the definition of the table:

```c
// r_alias.c and pdr_alias.c
float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};
```

The lighting of an alias vertex is the dot product of its normal with the light direction, so the table lets the renderer light a model without storing floats per vertex.
**Change:** the literals have an `f` suffix so the compiler does not promote them to `double`.
