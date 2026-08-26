from __future__ import annotations

import argparse
import csv
import logging
import signal
import threading
import time
from pathlib import Path
from typing import Any

import yaml

from usv_simulator.fmu_runner import UsvFmuRunner
from usv_simulator.models import ControlCommand, InitialState
from usv_simulator.mqtt_bridge import MqttBridge


LOGGER = logging.getLogger("usv_simulator")
CSV_FIELDS = (
    "time",
    "u_r",
    "v_r",
    "r",
    "x",
    "y",
    "heading_acc",
    "north_speed",
    "east_speed",
    "true_course_deg",
    "yaw_deg",
    "beta_deg",
)


def load_config(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        config = yaml.safe_load(file) or {}
    if not isinstance(config, dict):
        raise ValueError("Configuration root must be a mapping")
    return config


def resolve_path(base_dir: Path, value: str) -> Path:
    path = Path(value)
    return (base_dir / path).resolve() if not path.is_absolute() else path.resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description="USV FMI 2.0 + MQTT simulator")
    parser.add_argument("--config", default="config/usv.yaml")
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config = load_config(config_path)
    base_dir = config_path.parent
    logging.basicConfig(
        level=getattr(logging, str(config.get("logging", {}).get("level", "INFO")).upper()),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    stop_event = threading.Event()

    def request_stop(signum, frame) -> None:
        LOGGER.info("Stop requested")
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    simulation = config.get("simulation", {})
    step_size = float(simulation.get("step_size", 0.2))
    duration = float(simulation.get("duration", 0.0))
    realtime = bool(simulation.get("realtime", True))
    if step_size <= 0:
        raise ValueError("simulation.step_size must be positive")

    initial_config = config.get("initial", {})
    initial = InitialState(
        u0=float(initial_config.get("u0", 0.0)),
        v0=float(initial_config.get("v0", 0.0)),
        r0=float(initial_config.get("r0", 0.0)),
        yaw_deg0=float(initial_config.get("yaw_deg0", 0.0)),
    )
    # Geodetic origin belongs to the simulation initial state.
    protocol_config = dict(config.get("protocol", {}) or {})
    protocol_config["origin_longitude"] = float(initial_config.get("longitude", 0.0))
    protocol_config["origin_latitude"] = float(initial_config.get("latitude", 0.0))
    protocol_config["origin_height"] = float(initial_config.get("height", 0.0))
    control_config = config.get("control_defaults", {})
    defaults = ControlCommand(
        rudder_percent=float(control_config.get("rudder_percent", 0.0)),
        throttle_percent=float(control_config.get("throttle_percent", 0.0)),
        tip_buck_percent=float(control_config.get("tip_buck_percent", 0.0)),
    )

    fmu_path = resolve_path(base_dir, str(simulation.get("fmu_path", "../usv7.fmu")))
    output_config = config.get("output", {})
    csv_path = resolve_path(base_dir, str(output_config.get("csv_path", "../usv_state.csv")))
    flush_each_step = bool(output_config.get("flush_each_step", True))

    mqtt_bridge: MqttBridge | None = None
    mqtt_config = config.get("mqtt", {})
    if bool(mqtt_config.get("enabled", True)):
        mqtt_bridge = MqttBridge(
            mqtt_config,
            protocol_config,
            defaults,
            base_dir,
        )
        mqtt_bridge.start()

    try:
        with UsvFmuRunner(fmu_path) as runner, csv_path.open(
            "w", newline="", encoding="utf-8"
        ) as csv_file:
            runner.initialize(initial, defaults)
            writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
            writer.writeheader()

            next_tick = time.monotonic()
            while not stop_event.is_set() and (duration <= 0 or runner.time < duration - 1e-12):
                command = mqtt_bridge.latest_command() if mqtt_bridge else defaults
                state = runner.step(command, step_size)
                writer.writerow({"time": runner.time, **state.json_dict()})
                if flush_each_step:
                    csv_file.flush()
                if mqtt_bridge:
                    mqtt_bridge.publish_state(runner.time, state)

                if realtime:
                    next_tick += step_size
                    remaining = next_tick - time.monotonic()
                    if remaining > 0:
                        stop_event.wait(remaining)
                    elif remaining < -step_size:
                        LOGGER.warning("Simulation missed realtime deadline by %.3f s", -remaining)
                        next_tick = time.monotonic()

            LOGGER.info("Simulation finished at t=%.3f s; CSV=%s", runner.time, csv_path)
    finally:
        if mqtt_bridge:
            mqtt_bridge.stop()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
