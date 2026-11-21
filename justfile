ctr    := env_var_or_default("CTR", "podman")
userns := if ctr == "podman" { "--userns=keep-id" } else { "--user " + `id -u` + ":" + `id -g` }
root   := justfile_directory()
run    := ctr + " run --rm " + userns + " -v \"" + root + ":/src:Z\" -v sf-ccache:/ccache -w /src"
ds3    := env_var_or_default("DS3_DIR", env_var("HOME") + "/.local/share/Steam/steamapps/common/DARK SOULS III/Game")

img-build:
    {{ctr}} build -t sf-build      -f docker/build.Dockerfile .
    {{ctr}} build -t sf-wine-test  -f docker/wine-test.Dockerfile .

configure preset="win64-release":
    {{run}} sf-build cmake --preset {{preset}}

build preset="win64-release":
    {{run}} sf-build cmake --build --preset {{preset}}

probe:
    {{run}} sf-build cmake --preset win64-probe
    {{run}} sf-build cmake --build --preset win64-probe

test-host:
    cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests

test-asan:
    cmake --preset host-asan && cmake --build --preset host-asan && ctest --preset host-asan

test-cov:
    bash scripts/ci/run-coverage.sh

bench:
    cmake --preset host-bench -DSF_WERROR=ON && cmake --build --preset host-bench && ./build/host-bench/tests/bench/soulsframe_benchmarks

smoke:
    {{run}} sf-wine-test bash scripts/smoke-test.sh

package tag="v1.0.0":
    bash scripts/package-release.sh {{tag}} out/release

deploy preset="win64-release":
    cp "build/{{preset}}/dinput8.dll" "{{ds3}}/dinput8.dll"
    @echo "Steam launch option: WINEDLLOVERRIDES=\"dinput8=n,b\" %command%"

log:
    tail -f "{{ds3}}/soulsframe.log"

clean:
    rm -rf build
