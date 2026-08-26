from __future__ import annotations

import logging
import shutil
from pathlib import Path

from fmpy import extract, read_model_description
from fmpy.fmi2 import (
    FMU2Slave,
    defaultCallbacks,
    fmi2CallbackFunctions,
    fmi2CallbackLoggerTYPE,
)

from .models import ControlCommand, InitialState, VesselState


LOGGER = logging.getLogger(__name__)


class UsvFmuRunner:
    CONTROL_NAMES = ("Deltar", "rpm", "butk")
    INITIAL_NAMES = ("u_0", "v_0", "r_0", "Yaw_Deg__0")
    OUTPUT_NAMES = (
        "u_r",
        "v_r",
        "r",
        "x",
        "y",
        "headingAcc",
        "northSpeed",
        "eastSpeed",
        "TrueCourse_Deg_",
        "Yaw_Deg_",
        "Beta_Deg_",
    )

    def __init__(self, fmu_path: Path) -> None:
        self.fmu_path = fmu_path.resolve()
        if not self.fmu_path.is_file():
            raise FileNotFoundError(f"FMU not found: {self.fmu_path}")

        self.model_description = read_model_description(str(self.fmu_path), validate=True)
        if self.model_description.fmiVersion != "2.0":
            raise RuntimeError(f"Expected FMI 2.0, got {self.model_description.fmiVersion}")
        if self.model_description.coSimulation is None:
            raise RuntimeError("The FMU does not support Co-Simulation")

        variables = {variable.name: variable for variable in self.model_description.modelVariables}
        required = self.CONTROL_NAMES + self.INITIAL_NAMES + self.OUTPUT_NAMES
        missing = [name for name in required if name not in variables]
        if missing:
            raise RuntimeError(f"FMU variables missing: {', '.join(missing)}")

        self._value_references = {
            name: variables[name].valueReference for name in required
        }
        self._unzip_dir = Path(extract(str(self.fmu_path)))
        self._fmu = FMU2Slave(
            guid=self.model_description.guid,
            unzipDirectory=str(self._unzip_dir),
            modelIdentifier=self.model_description.coSimulation.modelIdentifier,
            instanceName="usv_python_instance",
        )
        self._logger_callback = fmi2CallbackLoggerTYPE(self._fmu_logger)
        self._callbacks = fmi2CallbackFunctions(
            self._logger_callback,
            defaultCallbacks.allocateMemory,
            defaultCallbacks.freeMemory,
            defaultCallbacks.stepFinished,
            None,
        )
        self.time = 0.0
        self._initialized = False

    def initialize(self, initial: InitialState, controls: ControlCommand) -> None:
        self._fmu.instantiate(visible=False, callbacks=self._callbacks, loggingOn=False)
        self._fmu.setupExperiment(startTime=0.0)
        self._fmu.enterInitializationMode()

        names = self.CONTROL_NAMES + self.INITIAL_NAMES
        values = (
            controls.rudder_percent,
            controls.throttle_percent,
            controls.tip_buck_percent,
            initial.u0,
            initial.v0,
            initial.r0,
            initial.yaw_deg0,
        )
        self._fmu.setReal([self._value_references[name] for name in names], values)
        self._fmu.exitInitializationMode()
        self.time = 0.0
        self._initialized = True
        LOGGER.info("FMU initialized: %s", self.fmu_path)

    def step(self, command: ControlCommand, step_size: float) -> VesselState:
        if not self._initialized:
            raise RuntimeError("FMU has not been initialized")

        self._fmu.setReal(
            [self._value_references[name] for name in self.CONTROL_NAMES],
            (command.rudder_percent, command.throttle_percent, command.tip_buck_percent),
        )
        self._fmu.doStep(
            currentCommunicationPoint=self.time,
            communicationStepSize=step_size,
        )
        self.time += step_size
        values = self._fmu.getReal(
            [self._value_references[name] for name in self.OUTPUT_NAMES]
        )
        return VesselState(*map(float, values))

    def close(self) -> None:
        try:
            if self._initialized:
                self._fmu.terminate()
        except Exception:
            LOGGER.exception("FMU termination failed")
        finally:
            try:
                self._fmu.freeInstance()
            finally:
                shutil.rmtree(self._unzip_dir, ignore_errors=True)
                self._initialized = False

    @staticmethod
    def _fmu_logger(component_environment, instance_name, status, category, message) -> None:
        if status <= 0:
            return
        instance = instance_name.decode("utf-8", errors="replace") if instance_name else "usv"
        category_text = category.decode("utf-8", errors="replace") if category else "fmu"
        message_text = message.decode("utf-8", errors="replace") if message else ""
        LOGGER.warning("FMU[%s][%s][%s]: %s", instance, category_text, status, message_text)

    def __enter__(self) -> "UsvFmuRunner":
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()
