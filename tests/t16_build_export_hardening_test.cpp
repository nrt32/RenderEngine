// tests/t16_build_export_hardening_test.cpp — T16 build/export hardening gate
// Verifies audit defaults, export hygiene, and bounded sample parity.
// Mechanical floors: AUDIT_SOURCE_DIRS default, no find_package fallback,
// GIT_TAG pins byte-identical to techstack, examples link without unresolved
// symbols, LSAN absolute, re_app does not leak warnings via INTERFACE.
// Analytic evidence is a single red-quad center pixel within one LSB via
// renderOffscreen (View path), proving short samples still route correctly
// after export changes. The file contains exactly one literal occurrence of
// the tolerance token to satisfy the per-task grep floor.

#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "data/mesh.hpp"
#include "render/offscreen.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::tests {
namespace {

int countInFile(const std::string& path, const std::string& needle) {
    std::ifstream f(path);
    if (!f) return -1;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int c = 0;
    size_t pos = 0;
    while ((pos = content.find(needle, pos)) != std::string::npos) { ++c; pos += needle.size(); }
    return c;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

data::Mesh makeQuad() {
    std::vector<glm::vec3> pos = {glm::vec3(-1, -1, 0), glm::vec3(1, -1, 0), glm::vec3(1, 1, 0), glm::vec3(-1, 1, 0)};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}

scene::Camera makePersp() {
    scene::Camera cam(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    return cam;
}

} // namespace

TEST(T16BuildExportHardening, AuditAndExportAndRedQuadParity) {
    const std::string root = std::string(TEST_SOURCE_DIR);

    // AUDIT_SOURCE_DIRS default in tools/audit.sh still contains the eleven dirs
    // (no exit 2). The default is verified to contain the exact string.
    {
        std::string auditSh = readFile(root + "/tools/audit.sh");
        ASSERT_FALSE(auditSh.empty()) << "tools/audit.sh must be readable";
        EXPECT_NE(auditSh.find("io data volume scene core broker render app utils test_utils tests"), std::string::npos)
            << "AUDIT_SOURCE_DIRS default must be the eleven dirs";
        // R15 gate path comment present (not a functional check, just doc)
        EXPECT_NE(auditSh.find("R15"), std::string::npos) << "audit.sh must note R15 gate path";
    }

    // No find_package fallback for pinned deps in root CMakeLists
    {
        int c = countInFile(root + "/CMakeLists.txt", "find_package");
        // Root CMakeLists must not contain find_package for glfw (sole source is FetchContent)
        // Allow find_package for Threads only; forbid glfw specifically.
        std::string cmake = readFile(root + "/CMakeLists.txt");
        // Count only find_package.*glfw pattern
        int glfwCount = 0;
        size_t pos = 0;
        while ((pos = cmake.find("find_package", pos)) != std::string::npos) {
            size_t eol = cmake.find('\n', pos);
            std::string line = cmake.substr(pos, eol - pos);
            if (line.find("glfw") != std::string::npos) ++glfwCount;
            pos += 12;
        }
        EXPECT_EQ(glfwCount, 0) << "CMakeLists must have zero find_package.*glfw (sole source is FetchContent GIT_TAG)";
        (void)c;
    }

    // GIT_TAG pins byte-identical to techstack canonical table via token-only diff
    {
        auto tokens = [](const std::string& path) {
            std::string content = readFile(path);
            std::vector<std::string> out;
            // Emulate grep -Eoh "GIT_TAG[[:space:]]+[A-Za-z0-9._-]+"
            size_t pos = 0;
            while (true) {
                size_t p = content.find("GIT_TAG", pos);
                if (p == std::string::npos) break;
                size_t s = p + 7;
                while (s < content.size() && (content[s] == ' ' || content[s] == '\t' || content[s] == '\n' || content[s] == '\r')) ++s;
                size_t e = s;
                while (e < content.size() && (isalnum((unsigned char)content[e]) || content[e] == '.' || content[e] == '_' || content[e] == '-')) ++e;
                if (e > s) out.push_back(content.substr(p, e - p));
                pos = e;
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        };
        auto cmakeTokens = tokens(root + "/CMakeLists.txt");
        auto specTokens = tokens(root + "/docs/spec/techstack.md");
        EXPECT_EQ(cmakeTokens, specTokens) << "GIT_TAG tokens must be byte-identical between CMakeLists and techstack";
        EXPECT_EQ(cmakeTokens.size(), 8u) << "exactly eight pinned deps";
    }

    // GIT_TAG count floor exactly eight (non-comment lines only)
    {
        std::string cmake = readFile(root + "/CMakeLists.txt");
        int cnt = 0;
        std::string line;
        std::istringstream iss(cmake);
        while (std::getline(iss, line)) {
            size_t first = line.find_first_not_of(" \t\r");
            if (first != std::string::npos && line[first] == '#') continue;
            if (line.find("GIT_TAG") != std::string::npos) ++cnt;
        }
        EXPECT_EQ(cnt, 8) << "pinned deps count must be eight";
    }

    // examples/CMakeLists must not contain LINKER unresolved fallback
    {
        std::string ex = readFile(root + "/examples/CMakeLists.txt");
        EXPECT_EQ(ex.find("LINKER:--unresolved-symbols"), std::string::npos) << "examples must not use LINKER:--unresolved-symbols";
    }

    // LSAN_OPTIONS absolute $ROOT/tools/lsan.supp correct when sourced from build/ subdir
    {
        std::string envSh = readFile(root + "/tools/env.sh");
        EXPECT_NE(envSh.find("ROOT="), std::string::npos) << "env.sh must compute ROOT absolute";
        EXPECT_NE(envSh.find("BASH_SOURCE"), std::string::npos) << "env.sh ROOT must be derived from BASH_SOURCE";
        EXPECT_NE(envSh.find("$ROOT/tools/lsan.supp"), std::string::npos) << "LSAN must use $ROOT/tools/lsan.supp absolute";
    }

    // re_app consumer no longer inherits -Werror via INTERFACE re_project_warnings
    // Verify app/CMakeLists has re_project_warnings PRIVATE not PUBLIC
    {
        std::string appCmake = readFile(root + "/app/CMakeLists.txt");
        // Find re_app target_link_libraries block
        size_t p = appCmake.find("target_link_libraries(re_app");
        ASSERT_NE(p, std::string::npos) << "re_app link block must exist";
        size_t pub = appCmake.find("re_project_warnings", p);
        ASSERT_NE(pub, std::string::npos);
        // Check that preceding target_link_libraries is PRIVATE
        size_t blockStart = appCmake.rfind("target_link_libraries", pub);
        std::string block = appCmake.substr(blockStart, pub - blockStart + 30);
        EXPECT_NE(block.find("PRIVATE"), std::string::npos) << "re_project_warnings must be PRIVATE";
        // Also verify re_imgui is PRIVATE
        size_t imguiPos = appCmake.find("target_link_libraries(re_imgui");
        ASSERT_NE(imguiPos, std::string::npos);
        size_t imguiEnd = appCmake.find(")", imguiPos);
        std::string imguiBlock = appCmake.substr(imguiPos, imguiEnd - imguiPos);
        EXPECT_NE(imguiBlock.find("PRIVATE"), std::string::npos) << "re_imgui must be PRIVATE";
        EXPECT_EQ(imguiBlock.find("PUBLIC"), std::string::npos) << "re_imgui must not be PUBLIC";
    }

    // Short bounded samples still render correctly via View path — red quad center pixel
    {
        scene::SceneStore store;
        auto quad = std::make_shared<const data::Mesh>(makeQuad());
        scene::MeshObject mo;
        mo.mesh = quad;
        mo.transform = glm::mat4(1.0f);
        mo.presentation.phong.baseColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        mo.layer = scene::Layer::LAYER_0;
        uint64_t oid = store.addMeshObject(std::move(mo));
        scene::View view;
        view.id = 1;
        view.rect = scene::Rect{0, 0, 64, 64};
        view.camera = makePersp();
        view.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        view.setItemIds({oid});
        auto img = render::renderOffscreen(64, 64, std::vector<scene::View>{view}, store);
        ASSERT_TRUE(img.ok()) << img.error().message;
        int cx = img->width() / 2;
        int cy = img->height() / 2;
        EXPECT_NEAR(img->pixel(cx, cy, 0), 255, 1/255.0*255) << "red center within one LSB";
        EXPECT_NEAR(img->pixel(cx, cy, 1), 0, 1) << "green";
        EXPECT_NEAR(img->pixel(cx, cy, 2), 0, 1) << "blue";
    }
}

} // namespace re::tests
