# USV FMU Simulator

Windows x64 C++ 船舶仿真服务。它加载 FMI 2.0 Co-Simulation `usv.fmu`，以 0.2 秒固定步长运行，并支持 MQTT 控制和状态发布。


## 初始值

编辑 `config/usv.ini`：

```ini
[initial]
longitude = 120.0
latitude = 30.0
height = 0.0
u0 = 0
v0 = 0
r0 = 0
yaw_deg0 = 0
```

这些值只在 FMU 初始化时设置一次。修改后需要重启程序。

每个 0.2 秒周期可变化的控制量是：

```ini
[control]
rudder_percent = 0
throttle_percent = 0
tip_buck_percent = 0
```


## 代码怎么分层

主循环只做三件事：读取控制量、调用 FMU、发布状态。

```text
src/main.cpp
    ├── FmuRunner              调用 FMU
    └── ICommunicationBridge   通信统一接口
          ├── MqttBridge        MQTT 连接和重连
          └── UdpBridge         UDP 收发

src/protocol_codec.cpp         1234/4321 报文编解码
```



最常见的修改位置：

- 只改 mqtt协议字段：编辑 `src/protocol_codec.cpp`；
- 只改服务器、端口、Topic：编辑 `config/usv.ini`；
- 增加 UDP 或替换 MQTT：编辑对应的 `*_bridge.cpp`，不要改 FMU；
- 修改 FMU 变量名或输入输出映射：编辑 `src/fmu_runner.cpp`。

## TODO

- 增加“连续一段时间油门、舵角、翻斗都为 0 后重置运动状态”的功能；
- 重置时保留当前经纬度和艏向角，将速度、横向速度和角速度清零；
- 需要在 `FmuRunner` 增加重置流程，并同步更新局部坐标原点。

## MQTT

项目内置了 MQTT-C 的必要源文件，无需安装 MQTT 客户端 SDK。来源和 MIT 许可证记录在 `THIRD_PARTY_NOTICES.md`。

启用 MQTT：

```ini
[communication]
enabled = true
type = mqtt

[mqtt]
host = 127.0.0.1
port = 1884
vessel_id = usv_001
```

## UDP

把配置改成：

```ini
[communication]
enabled = true
type = udp

[udp]
bind_host = 0.0.0.0
listen_port = 9001
remote_host = 127.0.0.1
remote_port = 9002
```


MQTT 协议映射
通用报文结构
{
  "head": {
    "packageSeq": 1,
    "packageType": 0,
    "time": "2026-08-23 18:00:00.000"
  },
  "content": {}
}
packageSeq：递增序号；接收端忽略小于或等于上一条序号的旧命令。
packageType：当前按标准协议填写 0。
time：本地时间，精确到毫秒。
1234：运动控制指令
MQTT Topic：


{
  "head": {
    "packageSeq": 1,
    "packageType": 0,
    "time": "2026-08-23 18:00:00.000"
  },
  "content": {
    "accL": 50,
    "accR": 50,
    "rudderL": 10,
    "rudderR": 10,
    "tipBuckL": 0,
    "tipBuckR": 0
  }
}

协议字段范围按 -100～100% 处理。

FMU 只有三个周期控制输入，因此采用：

usv.fmu 在模型内部完成百分比到物理量的换算，MQTT 外层不再重复换算。

换算比例均可在配置文件的 protocol 节调整。

艇状态 MQTT 

content 字段映射：

4321 字段	来源
longitude / latitude	以配置的 WGS-84 经纬度为原点，由 FMU 的 y 东向位移和 x 北向位移换算
height	配置的原点高度
speed	hypot(northSpeed, eastSpeed)
course	TrueCourse_Deg_
heading	Yaw_Deg_
headingAcc	FMU headingAcc
eastSpeed / northSpeed	FMU 同名输出
angularZ	FMU r
deviceState	配置值，默认 1
verticalSpeed / pitch / rolling	当前 FMU 无对应输出，暂填 0
angularX / angularY	当前 FMU 无对应输出，暂填 0
accX / accY / accZ	当前 FMU 无对应输出，暂填 0
如果后续 FMU 增加高度、横摇、纵摇和三轴加速度输出，应直接替换对应的暂定值。


# Third-party notices

MQTT-C
The project vendors selected source files from MQTT-C at commit 7a986a68ebea63921d4aab20a9d1b26a8b5f8c9d。

MQTT-C is distributed under the MIT License. The original license is included at third_party/mqtt-c/LICENSE。
