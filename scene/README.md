# scene/ — app-side scene description (SPEC §3.1)

`re::scene` value library (`STATIC`) — GL-free, RE-free. Owns every app-authored
type: `View{rect,plane,itemIds,gen}`, `Camera{pan/rotate/zoom/orbit → viewMatrix()}`,
`PlaneDesc{normal,point,Space}`, `SceneObject` family, plus `SceneStore`/`ViewStore`
stable handles + per-field `generation`. Links to `data`+`volume`+`glm` only;
`RE` keeps only translated `Re*` types (§3.1). Pure value semantics — copyable,
no `Handle`, `core` never included. Landed in V3.1 (T1).
