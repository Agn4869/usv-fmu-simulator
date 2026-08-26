from __future__ import annotations

import json
import logging
import math
import ssl
import threading
import time
from dataclasses import replace
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt

from .models import ControlCommand, VesselState


LOGGER = logging.getLogger(__name__)


class MqttBridge:
    def __init__(
        self,
        config: dict[str, Any],
        protocol: dict[str, Any],
        defaults: ControlCommand,
        base_dir: Path,
    ) -> None:
        self.config = config
        self.protocol = protocol
        self.defaults = defaults
        self._command = defaults
        self._command_lock = threading.Lock()
        self._last_command_at: float | None = None
        self._last_sequence: int | None = None
        self._connected = threading.Event()
        self._stopping = False
        self._state_sequence = 0

        vessel_id = str(config.get("vessel_id", "usv_001"))
        self.command_topic = str(protocol.get("command_topic", "1234"))
        self.state_topic = str(protocol.get("state_topic", "4321"))
        self.status_topic = str(config.get("status_topic", ""))

        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"usv-simulator-python-{vessel_id}",
            clean_session=True,
            protocol=mqtt.MQTTv311,
        )
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self.client.reconnect_delay_set(
            min_delay=int(config.get("reconnect_min_delay", 1)),
            max_delay=int(config.get("reconnect_max_delay", 30)),
        )
        self.client.max_queued_messages_set(20)
        if self.status_topic:
            self.client.will_set(self.status_topic, "offline", qos=1, retain=True)

        username = str(config.get("username", ""))
        if username:
            self.client.username_pw_set(username, str(config.get("password", "")))

        tls_config = config.get("tls", {}) or {}
        if bool(tls_config.get("enabled", False)):
            ca_file = str(tls_config.get("ca_file", ""))
            if ca_file:
                ca_path = Path(ca_file)
                if not ca_path.is_absolute():
                    ca_path = base_dir / ca_path
                self.client.tls_set(ca_certs=str(ca_path), tls_version=ssl.PROTOCOL_TLS_CLIENT)
            else:
                self.client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
            self.client.tls_insecure_set(bool(tls_config.get("insecure", False)))

    def start(self) -> None:
        host = str(self.config.get("host", "127.0.0.1"))
        port = int(self.config.get("port", 1883))
        keepalive = int(self.config.get("keepalive", 30))
        LOGGER.info("Starting MQTT connection to %s:%d", host, port)
        self.client.connect_async(host, port, keepalive)
        self.client.loop_start()

    def stop(self) -> None:
        self._stopping = True
        if self._connected.is_set() and self.status_topic:
            self.client.publish(self.status_topic, "offline", qos=1, retain=True)
            time.sleep(0.05)
        self.client.disconnect()
        self.client.loop_stop()
        self._connected.clear()

    def latest_command(self) -> ControlCommand:
        with self._command_lock:
            timeout = float(self.config.get("command_timeout_seconds", 0.0))
            action = str(self.config.get("timeout_action", "hold")).lower()
            if (
                timeout > 0
                and self._last_command_at is not None
                and time.monotonic() - self._last_command_at > timeout
                and action == "defaults"
            ):
                return self.defaults
            return self._command

    def publish_state(self, simulation_time: float, state: VesselState) -> None:
        if not self._connected.is_set():
            return
        self._state_sequence += 1
        latitude, longitude = self._position_to_geodetic(state.x, state.y)
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        payload = {
            "head": {
                "packageSeq": self._state_sequence,
                "packageType": int(self.protocol.get("package_type", 0)),
                "time": timestamp,
            },
            "content": {
                "longitude": longitude,
                "latitude": latitude,
                "height": float(self.protocol.get("origin_height", 0.0)),
                "speed": math.hypot(state.north_speed, state.east_speed),
                "course": state.true_course_deg,
                "heading": state.yaw_deg,
                "headingAcc": state.heading_acc,
                "eastSpeed": state.east_speed,
                "northSpeed": state.north_speed,
                "verticalSpeed": 0.0,
                "pitch": 0.0,
                "rolling": 0.0,
                "angularX": 0.0,
                "angularY": 0.0,
                "angularZ": state.r,
                "accX": 0.0,
                "accY": 0.0,
                "accZ": 0.0,
                "deviceState": int(self.protocol.get("device_state", 1)),
            },
        }
        payload["content"] = {
            key: value if not isinstance(value, float) or math.isfinite(value) else None
            for key, value in payload["content"].items()
        }
        info = self.client.publish(
            self.state_topic,
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")),
            qos=int(self.config.get("state_qos", 0)),
            retain=False,
        )
        if info.rc not in (mqtt.MQTT_ERR_SUCCESS, mqtt.MQTT_ERR_NO_CONN):
            LOGGER.warning("MQTT publish failed: %s", mqtt.error_string(info.rc))

    def _on_connect(self, client, userdata, connect_flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            LOGGER.error("MQTT connection rejected: %s", reason_code)
            return
        self._connected.set()
        client.subscribe(self.command_topic, qos=int(self.config.get("command_qos", 1)))
        if self.status_topic:
            client.publish(self.status_topic, "online", qos=1, retain=True)
        LOGGER.info("MQTT connected; subscribed to %s", self.command_topic)

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties) -> None:
        self._connected.clear()
        if self._stopping:
            LOGGER.info("MQTT disconnected")
        else:
            LOGGER.warning(
                "MQTT disconnected (%s); automatic reconnect remains active",
                reason_code,
            )

    def _on_message(self, client, userdata, message) -> None:
        if message.topic != self.command_topic:
            return
        try:
            payload = json.loads(message.payload.decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("command must be a JSON object")

            head = payload.get("head", {})
            content = payload.get("content", {})
            if not isinstance(head, dict) or not isinstance(content, dict):
                raise ValueError("1234 must contain head and content objects")

            sequence = head.get("packageSeq")
            if sequence is not None:
                sequence = int(sequence)
                if self._last_sequence is not None and sequence <= self._last_sequence:
                    LOGGER.warning("Ignored stale MQTT command sequence=%d", sequence)
                    return

            with self._command_lock:
                command = self._command
                throttle = self._average_percent(content, ("accL", "accM", "accR"))
                rudder = self._average_percent(content, ("rudderL", "rudderM", "rudderR"))
                tip_buck = self._average_percent(content, ("tipBuckL", "tipBuckR"))
                command = replace(
                    command,
                    # The regenerated FMU accepts the same percentage values
                    # as 1234. Do not convert them again in the bridge.
                    rudder_percent=rudder if rudder is not None else command.rudder_percent,
                    throttle_percent=(
                        throttle if throttle is not None else command.throttle_percent
                    ),
                    tip_buck_percent=(
                        tip_buck if tip_buck is not None else command.tip_buck_percent
                    ),
                )
                self._command = command
                self._last_command_at = time.monotonic()
                if sequence is not None:
                    self._last_sequence = sequence
            LOGGER.debug("MQTT control updated: %s", self._command)
        except Exception as exc:
            LOGGER.warning("Ignored invalid MQTT command: %s", exc)

    @staticmethod
    def _average_percent(payload: dict[str, Any], names: tuple[str, ...]) -> float | None:
        values: list[float] = []
        for name in names:
            if name in payload:
                value = float(payload[name])
                if not math.isfinite(value):
                    raise ValueError(f"{name} must be finite")
                if value < -100.0 or value > 100.0:
                    raise ValueError(f"{name} must be in [-100, 100]")
                values.append(value)
        return sum(values) / len(values) if values else None

    def _position_to_geodetic(self, north_m: float, east_m: float) -> tuple[float, float]:
        # WGS-84 local tangent-plane approximation. For the local ranges used
        # by this kinematic simulator it is substantially more accurate than a
        # single spherical Earth radius and does not require an extra package.
        semi_major_axis = 6_378_137.0
        flattening = 1.0 / 298.257223563
        eccentricity_squared = flattening * (2.0 - flattening)
        latitude0 = float(self.protocol.get("origin_latitude", 0.0))
        longitude0 = float(self.protocol.get("origin_longitude", 0.0))
        height0 = float(self.protocol.get("origin_height", 0.0))
        latitude_radians = math.radians(latitude0)
        sin_latitude = math.sin(latitude_radians)
        cos_latitude = math.cos(latitude_radians)
        denominator = math.sqrt(
            1.0 - eccentricity_squared * sin_latitude * sin_latitude
        )
        prime_vertical_radius = semi_major_axis / denominator
        meridian_radius = (
            semi_major_axis * (1.0 - eccentricity_squared) / denominator**3
        )

        latitude = latitude0 + math.degrees(
            north_m / (meridian_radius + height0)
        )
        longitude = longitude0
        if abs(cos_latitude) > 1e-12:
            longitude += math.degrees(
                east_m / ((prime_vertical_radius + height0) * cos_latitude)
            )
        return latitude, longitude
