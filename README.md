# USV FMU Simulator

船舶 FMU 仿真项目，提供 C++ 和 Python 两套相互独立的实现。

## 目录

- `cpp/`：C++ 仿真程序，支持 MQTT 和 UDP；
- `python/`：Python 版本，支持 FMU 调用和 MQTT 通信；
- `docs/protocol/`：通信协议说明；
- `docs/model/`：模型相关文件和说明。

## FMU 文件

仅提供一个slx的示例操作以及打包后可供调用的具体模板。

## 当前状态

C++ 版本已完成基础 FMU 调用、MQTT 通信、UDP 通信、mqtt编解码和 WGS-84 经纬度转换。
