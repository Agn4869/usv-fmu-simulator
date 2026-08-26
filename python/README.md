# USV FMU Simulator · Python

Python 实现：调用 FMU，并通过 MQTT 收发 1234 控制命令和 4321 状态信息。

## 启动

```powershell
cd python
python -m pip install -r requirements.txt
python main.py --config config\usv.yaml
```

仿真步长为 `0.2` 秒。按 `Ctrl+C` 停止。

运行前，请将有授权的 `.fmu` 文件放到本地目录，并在 `config\usv.yaml` 中设置 `fmu_path`。FMU 文件不会提交到公开仓库。

## 配置

`config\usv.yaml` 中可以设置 MQTT 地址、主题和初始位置：

```yaml
initial:
  longitude: 120.0
  latitude: 30.0
  height: 0.0
  u0: 0.0
  v0: 0.0
  r0: 0.0
  yaw_deg0: 0.0
```

`yaw_deg0: 0` 表示初始朝向正北。
