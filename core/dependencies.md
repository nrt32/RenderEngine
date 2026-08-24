# core/ third-party dependency lock

`core/` is the GL foundation module. All third-party dependencies it builds
against are pinned in the repository's root `CMakeLists.txt` via
`FetchContent_Declare(... GIT_TAG ...)` (SPEC §2, §6 dependency lock). Each
`GIT_TAG` is a release tag or commit SHA — never a branch name.

| Dependency | Pinned tag | Used by |
|---|---|---|
| GLFW | `3.4` | offscreen context (window/context creation) |
| glad2 | `v2.0.8` (commit `73db193`) | GL 4.6 core loader |
| GLM | `1.0.1` | math |
| Dear ImGui | `v1.92.9` | GUI (app/ layer) |
| GoogleTest | `v1.15.2` | tests/ |
| spdlog | `v1.14.1` | logging |
| stb | `2c980bb5` | image loading (io/ layer) |

The build declares these pins as, for example:

```cmake
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
```
