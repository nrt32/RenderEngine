// test_utils/capture_helpers.cpp — GPU capture helper façades (T18).

#include "test_utils/capture_helpers.hpp"

#include "render/linked_list_oit.hpp"
#include "render/slice_renderer.hpp"

namespace re::test_utils {

data::Result<std::uint32_t> readOitCapturedFragmentCount(
    render::LinkedListOIT& pipeline) {
    return pipeline.readCapturedFragmentCount();
}

data::Result<void> captureSliceCrossSection(
    render::SliceRenderer& renderer, const render::SliceScene& scene,
    const render::ClipPlane& plane, std::vector<glm::vec3>& out) {
    return renderer.captureCrossSection(scene, plane, out);
}

} // namespace re::test_utils
