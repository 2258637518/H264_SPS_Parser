# H264_SPS_Parser 🔍

> 一个纯标准 C++17 编写、**零第三方依赖（Header-Only）** 的高性能 H.264 / AVC 序列参数集（SPS）与 Annex B 码流语法解析器。

![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)
![Type](https://img.shields.io/badge/Type-Header--only-brightgreen.svg)
![Dependencies](https://img.shields.io/badge/Dependencies-Zero%20Libs-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

---

## 💡 写在前面

该项目是我音视频底层研发学习过程中的一部分。在音视频开发中，很多轻量工具、推流探针或嵌入式设备仅仅需要获取视频的分辨率、Profile 或帧率，却往往不得不链接数十 MB 庞大臃肿且存在许可限制的 FFmpeg 库。

为了彻底吃透 ITU-T H.264 国际标准，并摆脱对现成黑盒库的依赖，我实现了这个纯 C++ 零依赖的 SPS 语法解析器。虽然代码量极其精炼，但涵盖了底层码流切分、状态机、防竞争脱壳、变长熵解码到宏块几何推导的全套核心机制。

该文档由gemini生成,代码部分也经过gemini整理优化和注释,希望能够帮助到你.

---

## 🌟 核心特性

* **⚡ 纯单头文件与零依赖（Zero-Dependency Header-Only）**：
  * 无需链接 FFmpeg、无需链接任何第三方库，仅依赖 C++ 标准库。
  * 开箱即用，只需 `#include "h264_sps_parser.h"` 即可直接使用。
* **🔍 工业级 Annex B 起始码探测（StartCode State Machine）**：
  * 支持 `00 00 01`（3 字节）与 `00 00 00 01`（4 字节）起始码的自适应滑动窗口检测，将裸流精确切分为独立的 NALU 单元。
* **🛡️ 防竞争字节自动脱壳（Emulation Prevention Byte Removal）**：
  * 自动识别并剥除载荷中为了防碰撞插入的 `0x00 0x00 0x03`（EBSP $\to$ RBSP），杜绝按位读取时的全局比特偏移崩盘。
* **🧮 变长指数哥伦布解码器（Exponential-Golomb Decoder）**：
  * 手写大端序（MSB First）逐 bit 提取读取器 `BitReader`。
  * 完整实现 0 阶无符号指数哥伦布解码 `ue(v)` 与有符号指数哥伦布解码 `se(v)`。
* **🎯 全规格 Profile 支持（Baseline / Main / High Profile）**：
  * 不仅支持基础的 Baseline / Main Profile，还完整支持 **High Profile (Profile IDC = 100)** 及其扩展语法（色度采样格式、量化缩放矩阵 Scaling Matrix 跳过逻辑）。
* **📐 精准的几何宏块计算与画面裁切（Cropping）**：
  * 依据 H.264 规范自适应处理宏块乘数（16 像素）与隔行扫描场编码（32 像素）。
  * 自动根据色度采样格式（YUV420P / 422P / 444P）换算裁切单位，精确还原出真实物理分辨率（如将 1088 宏块高准确还原为 1080p）。

---

## 🏗️ 码流蜕变与解析管线

```text
Annex B 裸流数据 (含 00 00 01 起始码)
              │
              ▼ SplitNalu
NALU (NALU Header [1字节] + EBSP 载荷)
              │
              ▼ RemoveEmulationPreventionBytes (剔除 0x00 0x00 0x03 中的 0x03)
RBSP (纯净语法字节流)
              │
              ▼ BitReader + ReadUe (指数哥伦布解码)
SPS 核心语法树
              │
              ├─► profile_idc / level_idc / sps_id
              ├─► 基础宏块尺寸推导：
              │     width  = (pic_width_in_mbs_minus1 + 1) * 16
              │     height = (2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16
              └─► 画面裁切裁剪：
                    width  -= (crop_left + crop_right) * crop_unit_x
                    height -= (crop_top + crop_bottom) * crop_unit_y
```

---

## 🚀 快速上手与单元测试

### 1. 编译并运行内置单元测试

项目自带完整的单元测试套件（涵盖哥伦布码验证、0x03 脱壳验证、真实 1080p 带裁切 High Profile 解析验证以及 720p Annex B 码流探测验证）：

```bash
# 克隆仓库
git clone https://github.com/2258637518/H264_SPS_Parser.git
cd H264_SPS_Parser

# 构建并运行单元测试
cmake -B build -S .
cmake --build build --config Release

# 运行测试
./build/Release/test_h264_parser.exe   # Windows
# 或 ./build/test_h264_parser          # Linux / macOS
```

---

## 💻 API 使用示例

### 场景一：从一段 Annex B 裸流中一键解析（最简调用）

```cpp
#include <iostream>
#include <vector>
#include "h264_sps_parser.h"

int main() {
    std::vector<uint8_t> h264_stream = /* 读取的 .h264 文件或网络接收的码流 */;

    h264::SpsInfo sps;
    if (h264::ParseSpsFromAnnexB(h264_stream.data(), h264_stream.size(), sps)) {
        std::cout << "Profile     : " << sps.profile_idc << std::endl;
        std::cout << "Level       : " << sps.level_idc << std::endl;
        std::cout << "Resolution  : " << sps.width << "x" << sps.height << std::endl;
    } else {
        std::cerr << "未找到合法的 SPS 数据！" << std::endl;
    }
    return 0;
}
```

### 场景二：直接解析单个已知 SPS NALU

```cpp
const uint8_t sps_nalu[] = { 0x67, 0x42, ... }; // 含 0x67 Header 的完整 NALU

h264::SpsInfo sps;
if (h264::ParseSpsFromNalu(sps_nalu, sizeof(sps_nalu), sps)) {
    std::cout << "视频分辨率: " << sps.width << " x " << sps.height << std::endl;
}
```

---

## 📂 项目结构

```text
H264_SPS_Parser/
├── CMakeLists.txt                 # CMake 构建文件 (支持 FetchContent 与接口库)
├── LICENSE                        # MIT 开源许可证
├── README.md                      # 项目说明文档
├── .gitignore                     # Git 忽略配置
├── include/
│   └── h264_sps_parser.h          # 核心单头文件库 (所有逻辑在此定义)
└── tests/
    └── test_main.cpp              # 单元测试套件 (包含真实 1080p/720p 验证用例)
```

---

## 📜 开源协议

本项目采用 [MIT 许可证](LICENSE) 授权。
