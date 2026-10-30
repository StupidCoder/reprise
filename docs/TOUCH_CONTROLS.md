# Touch controls, tilt and orientation

The framework supplies portable input and native-resolution SDL overlays. Games
opt in and map inputs to reconstructed mechanics; this does not yet add touch or
tilt gameplay to Interceptor or Rock 'n Roll.

## Per-game options

Offer suitable schemes in the existing **Controls** options group. Typical choices
are Automatic, Keyboard / gamepad, Touch stick and Tilt, but expose only schemes
the game implements. `set_input_capabilities(InputCapabilities)` reports mobile,
touchscreen, connected gamepad and accelerometer support, before options are
restored and during play. Treat this notification as idempotent. Sensor presence
is detected without continuously sampling; `GameInput::tilt` can still be empty
if the sensor fails, so always have a usable fallback.

Use stable option/choice names for persistence. Mark scheme, sensitivity, invert
axes and related preferences `presentation=true` so loading a state preserves
them. Keep the selected preference separate from the effective scheme; do not
erase Tilt merely because a controller is temporarily connected. Prefer stable
choice lists per platform (rather than removing/reordering choices on hotplug),
with unavailable-hardware explanations and fallback. A Recalibrate action can
set a one-shot flag returned by `take_tilt_calibration_request()`.

The host hides virtual controls and suspends the tilt adapter while a physical
gamepad is connected. An opted-in game pauses when that controller disconnects.
Return an empty touch layout for native pointer/gesture interaction or a scheme
that needs no virtual controls. Keyboard/mouse input remains available.

## Touch layout and input

Override `touch_controls()` with stable IDs and safe-area fractions, for example:

```cpp
std::vector<platform::TouchControl> touch_controls() const override {
    using Kind = platform::TouchControl::Kind;
    if (!use_touch_) return {};
    std::vector<platform::TouchControl> result;
    if (!use_tilt_)
        result.push_back({"steer", "", Kind::stick, .02f,.50f,.28f,.46f});
    result.push_back({"thrust", "THRUST", Kind::slider, .88f,.32f,.09f,.60f});
    result.back().displayed_value = throttle_; // normalized game state
    result.push_back({"fire", "FIRE", Kind::button, .70f,.76f,.13f,.16f});
    result.push_back({"weapon", "WEAPON", Kind::button, .70f,.55f,.13f,.16f});
    result.push_back({"gear", "GEAR", Kind::button, .70f,.34f,.13f,.16f});
    return result;
}
```

This is an illustrative flight layout, not a verified Interceptor mapping.
Use `GameInput::touch_controls` by ID:

- Stick `x/y` are radial-dead-zone-adjusted values in [-1,1], positive y down.
  It floats around the initial contact and returns to zero on release.
- Slider `y` is [0,1], bottom to top, and stays where released. Apply it when
  `changed` is true; don't overwrite game state with an untouched default.
  Optional `displayed_value` feeds saved/keyboard/gamepad state back to the
  slider without generating input or interrupting a drag. Keep `initial` and
  layout geometry stable during a drag.
- Buttons have `held`, `pressed` and `released`. Use held for continuous fire,
  pressed for weapon cycling or gear toggles. A tap between simulation ticks
  retains both edges. Edges are consumed once, not repeated on catch-up ticks.

Each finger owns the control it pressed until release/cancellation, including
outside its bounds. A drag cannot acquire another control. Layout changes,
rotation, pause, loading and focus changes cancel captures. An opted-in layout
owns touch during gameplay: synthetic touch mouse events and native game fingers
are withheld to prevent double actions. Real mouse input remains separate.
Controls are drawn after the game filter at output resolution, within SDL's safe
area, for CPU and GPU games. They are absent from save screenshots and pause UI.

`GameInput::analog_stick` supplies the physical left stick in [-1,1], positive y
down, without game-specific dead zones. Existing digital controls remain intact.
Map analog values inside the game and retain its fixed simulation cadence.

## Tilt

Return `std::optional<platform::TiltConfig>` from `tilt_controls()` when the
selected effective scheme uses tilt. Its defaults are .35 G full scale, .05
radial dead zone and .08 seconds smoothing. Return `std::nullopt` otherwise.
The host opens the accelerometer only when needed, rotates samples into window
axes, and the adapter calibrates the first valid sample to neutral. `input.tilt`
is a normalized two-axis vector, positive x right and y down. Smoothing uses real
elapsed time rather than the original game's tick rate. Values are magnitude
clamped to one. Missing/invalid samples return no tilt and reset calibration.

Neutral resets on entry, pause/resume, rotation, quick load and a module's explicit
calibration request. Tilt configuration and neutral are device preferences, not
save-state physics. Rock 'n Roll can map this vector to ball steering; Interceptor
can map it to pitch/roll while retaining touch throttle and buttons. Decide axis
signs, sensitivity and acceleration against each game's mechanics. Keep the
original scheme available and explain the selected scheme in the options menu.

## Orientation and validation

`orientation()` defaults to `platform::Orientation::landscape`. Use `portrait`
for portrait games or `flexible` for layouts that adapt; all Layton modules use
flexible. The launcher is flexible. On Android the host requests sensor landscape,
sensor portrait or full sensor through its activity bridge; desktop windows remain
resizable. Games never call Android APIs or create native controls. Android may
apply device/window-management constraints to orientation requests.

Run `touch_controls_checks` / CTest `touch_and_tilt_controls` for multitouch,
finger ownership, tap edges, cancellation, slider feedback, calibration,
smoothing and missing sensors. For an actual game integration test on hardware:
rotate both landscape ways; pause and resume while holding controls; attach and
remove a controller; save/load with a nonzero throttle; restart with persisted
scheme settings; test with and without VHS on CPU and GPU rendering. Confirm
all actions remain reachable around cutouts and that no touch also clicks the game.
