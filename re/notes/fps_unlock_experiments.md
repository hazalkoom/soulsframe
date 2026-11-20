# DS3 High-Framerate / FPS Unlock Experiments (`M8`)

## 1. Why Dark Souls III Breaks Above 60 Hz

Dark Souls III couples simulation subsystems to a canonical `60 Hz` (`16.667 ms`) step. Raising presentation frame rates naively without decoupling or verifying engine subsystems introduces known gameplay regressions across 10 primary coupling points:

| # | Subsystem | Failure Mode Under Naive >60 Hz Delta Override |
|---|---|---|
| 1 | Weapon durability checks | Per-frame hit contact scans execute more often, accelerating durability loss per swing. |
| 2 | Gravity & fall damage integration | Impulse integration diverges across uneven high-rate timesteps, causing altered fall-damage thresholds. |
| 3 | Jump distance & ledge snapping | Sprint-jump arc length shrinks slightly at `>90 FPS` on unmodified vertical velocity steps. |
| 4 | Roll & parry i-frame counters | Frame-counted invincibility windows shorten when animation events fire without tick normalization. |
| 5 | Ladder slide velocity | Slide friction integrates per step, causing faster or jittery descent at unlocked rates. |
| 6 | FXR particle emission rates | Frame-stepped emitter bursts double spawn density at `120 Hz`. |
| 7 | Stamina / FP / HP regeneration | Per-tick regen accumulators drift slightly when fractional steps accumulate rounding error. |
| 8 | Havok cloth & ragdoll damping | Cape/hair constraints stiffen or jitter when substeps exceed expected damping ratios. |
| 9 | Moving elevator / platform sync | Entity platform attachment offsets drift briefly during frame-time spikes. |
| 10 | Menu & dialogue cursor repeat | UI key-repeat counters advance faster when tied directly to `Present()` cadence. |

## 2. Evaluated Strategies

| Strategy | Mechanism | Visual / Gameplay Fidelity | Risk | Status in SoulsFrame |
|---|---|---|---|---|
| **A (`fixed_step`)** | 60 Hz simulation accumulator + normalized interpolation alpha (`0..1`) + spiral-of-death clamping | Preserves exact 60 Hz gameplay timing while rendering intermediate frames smoothly | Medium (requires transform interpolation for full visual smoothing) | **Default (`modules.fps_unlock = false`)** |
| **B (`camera_only`)** | 60 Hz world simulation paired with high-rate camera & input delta-time | Eliminates camera panning stutter with minimal gameplay risk | Low-Medium | **Supported** |
| **C (`variable_dt`)** | Clamped variable `delta_ms` (`[1/360 s, 1/15 s]`) across engine delta reads | Experimental only; breaks frame-counted animations at extreme FPS | High | **Experimental only** |
| **D (naive override)** | Unclamped raw delta override | Known bugs on ladders, falls, cloth, durability | Very High | **Rejected** |

## 3. Safety Decision (M8)

1. `modules.fps_unlock` defaults to `false` in `config/soulsframe.toml.example`.
2. Strategy A (`fixed_step` at `sim_tick_hz = 60`, `max_substeps = 4`) is the default strategy implemented in `sf::timing::FixedStepStepper`.
3. `sf::timing::evaluate_fps_unlock_safety()` strictly enforces **read-only dry-run mode** (`dry_run = true`) whenever `profile.engine_hooks_allowed == false` or either `frame_dt_ptr` or `frame_limiter` is unverified for the host executable SHA-256.
