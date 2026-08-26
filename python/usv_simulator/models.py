from __future__ import annotations

from dataclasses import asdict, dataclass
from math import isfinite
from typing import Any


@dataclass(frozen=True)
class InitialState:
    u0: float = 0.0
    v0: float = 0.0
    r0: float = 0.0
    yaw_deg0: float = 0.0


@dataclass(frozen=True)
class ControlCommand:
    # 1234/FMU control inputs are percentages in [-100, 100].
    rudder_percent: float = 0.0
    throttle_percent: float = 0.0
    tip_buck_percent: float = 0.0


@dataclass(frozen=True)
class VesselState:
    u_r: float
    v_r: float
    r: float
    x: float
    y: float
    heading_acc: float
    north_speed: float
    east_speed: float
    true_course_deg: float
    yaw_deg: float
    beta_deg: float

    def json_dict(self) -> dict[str, Any]:
        return {
            key: value if isfinite(value) else None
            for key, value in asdict(self).items()
        }
