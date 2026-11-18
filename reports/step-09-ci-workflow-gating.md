# Step 09: Smart CI workflow triggering and path gating

Date: 2026-11-17
Branch: `feat/oc-tasks`

## What changed

Reduced GitHub Actions usage without splitting the repository into many more workflows.

### Trigger policy

- `ci.yml` now runs on pull requests targeting `main` or `develop`, pushes to `main` or
  `develop`, and explicit `workflow_dispatch` runs.
- Feature/fix/test/docs/CI branch pushes no longer launch the full CI suite. Their pull
  request runs the appropriate checks instead.
- `security.yml` now follows the same PR/base-branch push policy while retaining its weekly
  scheduled audit.
- CI concurrency now includes the workflow name, preventing unrelated workflows from
  accidentally sharing cancellation groups.

### Path-aware job selection

Added one `changes` classification job using `dorny/paths-filter`:

| Change type | Jobs selected |
|---|---|
| Markdown/docs/report-only | guards + Markdown lint |
| Python telemetry changes | guards + Python stdlib self-tests |
| C++/tests/build/config changes | guards + static analysis, host tests, benchmarks, coverage, fuzz, Windows build, Wine smoke |
| Scripts/docker changes | guards + broad validation and Docker verification |
| Workflow/dependabot changes | broad validation, docs/Python checks, and Docker verification |

The existing single `ci.yml` remains the source of truth; no duplicated reusable workflow
tree was introduced.

## Verification

- `bash scripts/ci/check-guards.sh`: passed.
- `cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests`: passed.
- `python3 tools/py/analyze_capture.py --self-test`: passed.
- `python3 tools/py/plot_gpu_passes.py --self-test`: passed.
- Both modified workflow files parse successfully with PyYAML.

## Follow-up

After this lands, monitor Actions usage and skipped-check behavior on one docs-only PR,
one Python-only PR, and one C++ PR. If branch protection requires a stable check name,
keep the job IDs stable and use the path-gated jobs as optional checks or add a small
always-running required-summary job.
