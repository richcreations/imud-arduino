# Glossary

Plain-language definitions of the terms this library's documentation uses.
Written for someone using the data, not someone implementing the filter that
produces it.

For the byte-level meaning of each field, see [PROTOCOL.md](PROTOCOL.md).

---

## Attitude

The orientation of something in 3-D space — which way it's pointing and how
it's tilted. This library's whole job is delivering attitude from the imud
daemon to your sketch. It's usually expressed as three angles (heading,
pitch, roll) or as a [quaternion](#quaternion).

## NED

**N**orth, **E**ast, **D**own — the reference frame all of imud's attitude
data uses. X points north, Y points east, and **Z points down** toward the
centre of the earth.

Z pointing *down* surprises people, but it's the marine and aviation
convention, and it's why a sensor sitting flat and still reports roughly
`accel_z = -9.81` rather than `+9.81`.

## Heading

Which way the front of the vehicle is pointing, in degrees clockwise from
north: 0° = north, 90° = east, 180° = south, 270° = west.

`heading_deg` is **magnetic** heading — measured from magnetic north, which
is where a compass points. It's already in degrees; unlike pitch/roll/yaw it
needs no conversion.

## Magnetic vs true heading

**Magnetic north** is where the earth's magnetic field points. **True north**
is the geographic north pole. They are not in the same place, and the
difference between them depends on where you are standing.

- `packet().heading_deg` — magnetic heading, always available.
- `imud.trueHeading()` — true heading, or `-1.0f` if the daemon doesn't know
  the local [declination](#declination) yet.

Charts and maps use true north. A compass gives magnetic. Mixing them up is
a classic navigation error, which is why this library keeps them clearly
separate and refuses to guess.

## Declination

*(also called magnetic variation)*

The local angle between magnetic north and true north. Roughly:

```
true heading = magnetic heading + declination
```

It varies by location — several degrees east in one place, several degrees
west in another — and drifts slowly over years as the earth's field moves.

imud reports it as `declination_deg` (positive = east). It's only meaningful
when the `IMUD_FLAG_DECLINATION_VALID` flag is set; until then
`imud_true_heading()` returns `-1.0f` rather than a wrong answer.

## Pitch, roll, yaw

*(collectively: Euler angles)*

The three tilt angles, in the [NED](#ned) frame:

- **Pitch** — nose up or down. Positive = bow up.
- **Roll** — tilt side to side. Positive = starboard (right) side up.
- **Yaw** — rotation about the vertical axis. Closely related to heading.

**All three are in RADIANS**, not degrees — this catches nearly everyone
once. Use `imud_rad_to_deg()` to convert:

```cpp
Serial.println(imud_rad_to_deg(p.roll));   // degrees
```

`heading_deg` is the exception: it's already degrees, as the name says.

## Quaternion

A four-number representation of orientation (`quat_w/x/y/z`). It describes
exactly the same thing as pitch/roll/yaw, but without
[gimbal lock](#gimbal-lock) and with cleaner maths for combining rotations.

If you just want to display an angle, use pitch/roll/yaw and ignore this. If
you're rotating vectors, driving a 3-D model, or chaining rotations
together, use the quaternion.

## Gimbal lock

The failure mode Euler angles have when pitch approaches straight up or
straight down: two of the three axes line up and you lose a degree of
freedom, making heading jumpy or undefined. It's the main reason
[quaternions](#quaternion) exist. Not a concern for boats; very much one for
drones and robot arms.

## Covariance

The filter's own estimate of **how uncertain it is**. `cov[9]` is a 3×3
matrix; the diagonal entries (`cov[0]`, `cov[4]`, `cov[8]`) are the variances
of the three attitude axes — bigger means less confident.

Most applications can ignore this and just watch the
[convergence](#fusion-convergence) flag instead.

## Fusion convergence

When the filter has settled and its estimates are trustworthy, indicated by
`IMUD_FLAG_FUSION_CONVERGED`. It takes some seconds after startup.

**Don't display or act on attitude before this flag is set.** Early readings
are the filter still finding its feet.

## Kalman filter / MEKF

The algorithm that combines gyroscope, accelerometer and magnetometer
readings into one attitude estimate that's better than any of them alone.
Each sensor has different weaknesses — gyros drift over time, accelerometers
are fooled by acceleration, magnetometers by nearby metal — and the filter
weighs them against each other continuously.

MEKF = Multiplicative Extended Kalman Filter, the quaternion-friendly
variant imud uses. This runs entirely in the daemon; the library just
receives the results.

## Gyro bias

The small nonzero rotation rate a gyroscope reports while sitting perfectly
still. Left uncorrected it makes heading drift steadily. The filter
estimates it continuously and reports it as `gyro_bias_x/y/z`
(`IMUD_FLAG_GYRO_CAL` indicates the correction is being applied).

## Rate of turn

How fast heading is changing, in **degrees per minute**, positive when
turning right. Marine displays conventionally show this alongside heading.
Note the unit: per *minute*, not per second.

## Heave

Vertical motion — how far the vessel is rising and falling, in metres,
positive up (`heave_m`), plus its rate in m/s (`heave_rate`). Gated by
`IMUD_FLAG_HEAVE_VALID`.

## Sea state

Statistics describing wave conditions, gated by `IMUD_FLAG_WAVE_VALID`:

- `wave_height_m` — **significant wave height (Hs)**, roughly the average
  height of the largest third of waves. It's the number marine forecasts
  quote, and individual waves can be considerably bigger.
- `wave_period_s` — mean time between waves.
- `roll_period_s` / `pitch_period_s` — how long one full roll or pitch cycle
  takes for this particular vessel.
- `roll_amplitude` / `pitch_amplitude` — how far it's swinging (2σ, radians).

## Magnetic anomaly

`mag_anomaly` measures how far the sensed field strength is from the
expected local value — a way of noticing that something magnetic is nearby
(an engine, a speaker, a steel bulkhead) and distorting the compass.
`mag_residual` is a related compass-health measure: the running average of
how much the magnetometer disagrees with the rest of the filter.

## Innovation

The gap between what the filter **predicted** a sensor would read and what
it **actually** read. Small innovations mean the model is tracking reality;
large ones mean something is off — a bad sensor reading, or an assumption
that no longer holds.

The four v17 health fields below all describe how the filter is handling its
innovations.

## Huber weight (`innov_weight`)

A **robustness** measure. Rather than trusting every reading equally, the
filter caps the influence of surprisingly large ones so a single bad sample
can't yank the estimate around. `innov_weight` is the running average of how
much capping is happening:

- `1.0` — nothing is being capped. Healthy.
- trending toward `0.33` — sustained capping; the filter is constantly
  fighting readings it doesn't believe.

## Gate rejection (`innov_reject`)

Readings that are *so* far off they're discarded outright rather than merely
capped. `innov_reject` is the fraction being thrown away: `0.0` means
nothing is, and a persistently nonzero value points at a genuinely misbehaving
sensor.

## NIS (`nis_accel`, `nis_mag`)

**Normalised Innovation Squared** — a check on whether the filter's own
confidence is honest. It compares how wrong the filter actually is against
how wrong it *expected* to be.

- **`1.0` is correct** — the [covariance](#covariance) accurately predicts
  the real spread.
- **Above 1** — over-confident; it's more wrong than it thinks.
- **Well below 1** — over-cautious; carrying more uncertainty than needed.

Where `innov_weight` and `innov_reject` say how hard the robustness
machinery is working, NIS says whether the underlying noise model is right.

Two caveats: these move slowly (roughly a 30-second time constant), so read
them as trends rather than per-packet values. And `nis_mag` reads lower in
3-D magnetometer mode *by design* — don't compare it across magnetometer
modes, and don't read a low value as a fault.

## Wire version

The version number of the **packet layout**, not of the library or the
daemon. This library pins one wire version and rejects every other one, so
that a layout change can never be silently misread as valid data.

The current release speaks **wire v17** and needs **imud ≥ 1.7**. Pairing it
with an older daemon produces no packets at all — see [the wire-sync
warning](../README.md#wire-sync-warning).

## CRC32

A checksum covering each packet. The sender computes it, the receiver
recomputes it, and a mismatch means the packet got corrupted in transit and
is discarded. `crcErrors()` counts these.

## Magic number

The four bytes `IMUD` at the start of every packet. They mark where a packet
begins, which is how the parser finds frame boundaries in a raw byte stream
and recovers alignment after corruption.

## Multicast

A way of sending one stream to many listeners at once: the sender publishes
to a **group address** (imud uses `239.255.0.1`) and any device that has
joined that group receives it, without the sender needing to know who's
listening.

Efficient in principle, unreliable in practice on consumer WiFi — many
access points silently drop multicast. See [the UDP section of
TROUBLESHOOTING.md](TROUBLESHOOTING.md#udp-rate-stays-00-hz).
