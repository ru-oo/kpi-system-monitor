# Deliverable note — ego-pose signal gap (Tactical page)

## Problem
The Tactical page renders the ego vehicle in the **map frame**, but the current
DBC has **no map-frame ego pose**. `Vehicle_Status` (0x101) carries only
speed/steering; `Route_Status` (0x104) carries `progressM` (along-track) and
`amclErrorM` (cross-track magnitude). Neither is a map-frame `(x, y, yaw)`.

## Current stopgap (interim, straight-route only)
`TacticalMap.qml` derives an approximate ego position from
`config.tactical.route_start_{x,y}` + `route_heading_deg`:

    ego_x = start_x + progressM·cosθ − amclErrorM·sinθ
    ego_y = start_y + progressM·sinθ + amclErrorM·cosθ

This is **only valid for a straight corridor** with a known start/heading. It
breaks on curved/loop courses (the very thing map-frame goals enable). It is
clearly gated in the renderer so it can be replaced without touching the rest.

## Recommendation (Jetson side — needs team agreement)
Add a map-frame ego-pose CAN message — **`Ego_Pose`** (or extend a
`Localization_Status`) broadcast at ~10 Hz from `/amcl_pose`:

| Signal   | Type   | Scale   | Notes                       |
|----------|--------|---------|-----------------------------|
| Ego_X    | int16  | 0.01 m  | map frame, signed (±327 m)  |
| Ego_Y    | int16  | 0.01 m  | map frame, signed           |
| Ego_Yaw  | int16  | 0.1 deg | map heading (±180°)         |
| Cov_XY   | uint8  | 0.01 m  | 1-σ position std (optional) |
| Cov_Yaw  | uint8  | 0.1 deg | 1-σ heading std (optional)  |
| Counter  | uint8  | —       | liveness                    |

Then `KpiData` gains `egoX/egoY/egoYaw(+cov)` and `TacticalMap.qml` swaps the
stopgap for the real pose (one binding change). Covariance feeds the optional
localization-uncertainty shaded radius noted in the design.

Until then the interim derivation is acceptable for the straight-road demo only.
