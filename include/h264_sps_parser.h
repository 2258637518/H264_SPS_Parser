#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <iostream>

namespace h264 {

/**
 * @brief NALU 单元描述结构体
 */
struct Nalu {
    int start_code_len = 0;        // 起始码长度 (3 或 4)
    const uint8_t* data = nullptr; // 指向 NALU Header 首地址
    size_t size = 0;               // NALU 总字节数 (含 Header)
    uint8_t type = 0;              // NALU 类型 (7=SPS, 8=PPS, 5=IDR, 1=Slice 等)
};

/**
 * @brief SPS 解析结果元数据
 */
struct SpsInfo {
    uint32_t profile_idc = 0;      // 编码 Profile (66=Baseline, 77=Main, 100=High)
    uint32_t level_idc = 0;        // 编码 Level (如 31=Level 3.1, 40=Level 4.0, 41=Level 4.1)
    uint32_t sps_id = 0;           // SPS 序列参数集 ID (0~31)
    uint32_t chroma_format_idc = 1; // 色度采样格式 (1=4:2:0, 2=4:2:2, 3=4:4:4, 0=单色)

    uint32_t width = 0;            // 实际物理显示宽度 (已计算 Cropping 裁切)
    uint32_t height = 0;           // 实际物理显示高度 (已计算场编码与 Cropping 裁切)

    uint32_t crop_left = 0;        // 左边界裁切量
    uint32_t crop_right = 0;       // 右边界裁切量
    uint32_t crop_top = 0;         // 上边界裁切量
    uint32_t crop_bottom = 0;      // 下边界裁切量
};

/**
 * @brief 高性能比特流读取器 (MSB First / 大端序逐 bit 提取)
 */
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), bit_pos_(0) {}

    // 1. 读取单个 bit (0 或 1)
    uint32_t ReadBit() {
        if (bit_pos_ >= size_ * 8) {
            return 0; // 越界防护
        }

        size_t byte_idx = bit_pos_ / 8;
        size_t bit_offset = 7 - (bit_pos_ % 8);
        bit_pos_++;
        return (data_[byte_idx] >> bit_offset) & 0x01;
    }

    // 2. 读取连续 n 个 bit 并拼装为无符号整型
    uint32_t ReadBits(int n) {
        uint32_t val = 0;
        for (int i = 0; i < n; ++i) {
            val = (val << 1) | ReadBit();
        }
        return val;
    }

    // 获取当前读取游标所在的比特位偏移
    size_t GetBitPos() const { return bit_pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bit_pos_;
};

/**
 * @brief 0 阶无符号指数哥伦布解码 (Exponential-Golomb, ue(v))
 * @formula Value = (2^M - 1) + INFO, 其中 M 为前导 0 个数
 */
inline uint32_t ReadUe(BitReader& reader) {
    int leadingZeros = 0;

    // 统计前导 0 的个数，直到遇到 1
    while (reader.ReadBit() == 0 && leadingZeros < 32) {
        leadingZeros++;
    }

    if (leadingZeros == 0) {
        return 0;
    }

    // 读取 leadingZeros 位长度的 INFO，并套用数学公式还原
    uint32_t info = reader.ReadBits(leadingZeros);
    return (1U << leadingZeros) - 1 + info;
}

/**
 * @brief 0 阶有符号指数哥伦布解码 (se(v))
 * 映射规则: ue 为奇数时为正，为偶数时为负
 */
inline int32_t ReadSe(BitReader& reader) {
    uint32_t ue = ReadUe(reader);
    int32_t sign = (ue & 1) ? 1 : -1;
    return sign * static_cast<int32_t>((ue + 1) / 2);
}

/**
 * @brief 匹配起始码长度 (00 00 01 返回 3, 00 00 00 01 返回 4)
 */
inline int MatchStartCode(const uint8_t* data, size_t bytes_remaining) {
    if (bytes_remaining < 3) {
        return 0;
    }

    if (bytes_remaining >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
        return 4;
    }

    if (bytes_remaining >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        return 3;
    }

    return 0;
}

/**
 * @brief 查找下一个起始码的位置
 */
inline int FindNextStartCode(const uint8_t* data, size_t size, int& start_code_len) {
    if (!data || size < 3) {
        return -1;
    }

    for (size_t i = 0; i <= size - 3; ++i) {
        int len = MatchStartCode(data + i, size - i);
        if (len > 0) {
            start_code_len = len;
            return static_cast<int>(i);
        }
    }
    return -1;
}

/**
 * @brief 将一段 H.264 Annex B 字节流切分为独立的 NALU 列表
 */
inline std::vector<Nalu> SplitNalu(const uint8_t* data, size_t size) {
    std::vector<Nalu> nalu_list;
    if (!data || size < 4) return nalu_list;

    int first_code_len = 0;
    int first_pos = FindNextStartCode(data, size, first_code_len);
    if (first_pos < 0) return nalu_list;

    int cur_start = first_pos;
    int cur_code_len = first_code_len;

    while (cur_start < static_cast<int>(size)) {
        int next_code_len = 0;
        int next_search_offset = cur_start + cur_code_len;

        int next_pos = -1;
        if (next_search_offset < static_cast<int>(size)) {
            next_pos = FindNextStartCode(data + next_search_offset, size - next_search_offset, next_code_len);
            if (next_pos >= 0) {
                next_pos += next_search_offset;
            }
        }

        Nalu nalu;
        nalu.start_code_len = cur_code_len;
        nalu.data = data + cur_start + cur_code_len;

        if (next_pos >= 0) {
            nalu.size = next_pos - next_search_offset;
        } else {
            nalu.size = size - next_search_offset;
        }

        if (nalu.size > 0) {
            nalu.type = nalu.data[0] & 0x1F;
            nalu_list.push_back(nalu);
        }

        if (next_pos < 0) break;

        cur_start = next_pos;
        cur_code_len = next_code_len;
    }

    return nalu_list;
}

/**
 * @brief 防竞争字节脱壳 (EBSP -> RBSP)
 * 扫描并剔除所有 0x00 0x00 0x03 中的 0x03
 */
inline std::vector<uint8_t> RemoveEmulationPreventionBytes(const uint8_t* ebsp_data, size_t ebsp_size) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(ebsp_size);
    size_t i = 0;
    while (i < ebsp_size) {
        if (i + 2 < ebsp_size && ebsp_data[i] == 0x00 && ebsp_data[i + 1] == 0x00 && ebsp_data[i + 2] == 0x03) {
            rbsp.push_back(ebsp_data[i]);
            rbsp.push_back(ebsp_data[i + 1]);
            i += 3; // 跳过 0x03
        } else {
            rbsp.push_back(ebsp_data[i]);
            i++;
        }
    }
    return rbsp;
}

/**
 * @brief 解析 H.264 SPS 语法树字段 (支持 Baseline / Main / High Profile 全规格)
 * @param rbsp_data 已经完成 0x03 脱壳的 RBSP 数据首地址 (不含 NALU Header)
 * @param rbsp_size RBSP 数据总字节数
 */
inline bool ParseSpsBasic(const uint8_t* rbsp_data, size_t rbsp_size, SpsInfo& sps) {
    if (!rbsp_data || rbsp_size < 3) {
        return false;
    }

    BitReader reader(rbsp_data, rbsp_size);

    sps.profile_idc = reader.ReadBits(8);
    reader.ReadBits(8); // 跳过 constraint_set_flags 与保留位
    sps.level_idc = reader.ReadBits(8);
    sps.sps_id = ReadUe(reader);

    // High Profile (100) 及以上扩展语法支持
    sps.chroma_format_idc = 1; // 默认 4:2:0
    if (sps.profile_idc == 100 || sps.profile_idc == 110 ||
        sps.profile_idc == 122 || sps.profile_idc == 244 || sps.profile_idc == 44 ||
        sps.profile_idc == 83  || sps.profile_idc == 86  || sps.profile_idc == 118 ||
        sps.profile_idc == 128 || sps.profile_idc == 138 || sps.profile_idc == 139 ||
        sps.profile_idc == 134 || sps.profile_idc == 73) {

        sps.chroma_format_idc = ReadUe(reader);
        if (sps.chroma_format_idc == 3) {
            reader.ReadBit(); // separate_colour_plane_flag
        }
        ReadUe(reader); // bit_depth_luma_minus8
        ReadUe(reader); // bit_depth_chroma_minus8
        reader.ReadBit(); // qpprime_y_zero_transform_bypass_flag

        uint32_t seq_scaling_matrix_present_flag = reader.ReadBit();
        if (seq_scaling_matrix_present_flag) {
            int count = (sps.chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < count; ++i) {
                uint32_t seq_scaling_list_present_flag = reader.ReadBit();
                if (seq_scaling_list_present_flag) {
                    int size_of_scaling_list = (i < 6) ? 16 : 64;
                    int last_scale = 8;
                    int next_scale = 8;
                    for (int j = 0; j < size_of_scaling_list; ++j) {
                        if (next_scale != 0) {
                            int32_t delta_scale = ReadSe(reader);
                            next_scale = (last_scale + delta_scale + 256) % 256;
                        }
                        last_scale = (next_scale == 0) ? last_scale : next_scale;
                    }
                }
            }
        }
    }

    // 1. 读取帧序号与 POC 相关字段
    uint32_t log2_max_frame_num_minus4 = ReadUe(reader);
    uint32_t pic_order_cnt_type = ReadUe(reader);

    if (pic_order_cnt_type == 0) {
        uint32_t log2_max_pic_order_cnt_lsb_minus4 = ReadUe(reader);
    } else if (pic_order_cnt_type == 1) {
        reader.ReadBit(); // delta_pic_order_always_zero_flag
        ReadSe(reader);   // offset_for_non_ref_pic
        ReadSe(reader);   // offset_for_top_to_bottom_field
        uint32_t num_ref_frames_in_pic_order_cnt_cycle = ReadUe(reader);
        for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i) {
            ReadSe(reader); // offset_for_ref_frame
        }
    }

    // 2. 读取参考帧数与间隙标志
    uint32_t max_num_ref_frames = ReadUe(reader);
    uint32_t gaps_in_frame_num_value_allowed_flag = reader.ReadBit();

    // 3. 读取宏块宽高与帧场标志位
    uint32_t pic_width_in_mbs_minus1 = ReadUe(reader);
    uint32_t pic_height_in_map_units_minus1 = ReadUe(reader);
    uint32_t frame_mbs_only_flag = reader.ReadBit();

    // 4. 计算基础未裁切宽高 (宏块基准 16 像素，场编码为 32 像素)
    sps.width = (pic_width_in_mbs_minus1 + 1) * 16;
    sps.height = frame_mbs_only_flag == 0 ? (pic_height_in_map_units_minus1 + 1) * 32 : (pic_height_in_map_units_minus1 + 1) * 16;

    // 5. 补充场编码判断与 8x8 推导标志
    if (!frame_mbs_only_flag) {
        reader.ReadBit(); // mb_adaptive_frame_field_flag
    }
    reader.ReadBit(); // direct_8x8_inference_flag

    // 6. 画面裁切判定 (frame_cropping_flag)
    uint32_t frame_cropping_flag = reader.ReadBit();

    if (frame_cropping_flag) {
        sps.crop_left = ReadUe(reader);
        sps.crop_right = ReadUe(reader);
        sps.crop_top = ReadUe(reader);
        sps.crop_bottom = ReadUe(reader);

        // 计算裁切单位 (基于色度格式)
        uint32_t crop_unit_x = 1;
        uint32_t crop_unit_y = 2 - frame_mbs_only_flag;

        if (sps.chroma_format_idc == 1) { // 4:2:0
            crop_unit_x = 2;
            crop_unit_y = 2 * (2 - frame_mbs_only_flag);
        } else if (sps.chroma_format_idc == 2) { // 4:2:2
            crop_unit_x = 2;
            crop_unit_y = 1 * (2 - frame_mbs_only_flag);
        }

        // 扣除裁切黑边，更新真实宽高
        sps.width -= (sps.crop_left + sps.crop_right) * crop_unit_x;
        sps.height -= (sps.crop_top + sps.crop_bottom) * crop_unit_y;
    }
    return true;
}

/**
 * @brief 便捷接口：直接输入单个完整 SPS NALU 进行解析 (含 NALU Header)
 */
inline bool ParseSpsFromNalu(const uint8_t* nalu_data, size_t nalu_size, SpsInfo& sps) {
    if (!nalu_data || nalu_size < 2) return false;

    // 验证 NALU 类型是否为 SPS (Type 7)
    uint8_t nalu_type = nalu_data[0] & 0x1F;
    if (nalu_type != 7) return false;

    // 跳过 1 字节 NALU Header，对载荷进行 0x03 脱壳
    std::vector<uint8_t> rbsp = RemoveEmulationPreventionBytes(nalu_data + 1, nalu_size - 1);
    return ParseSpsBasic(rbsp.data(), rbsp.size(), sps);
}

/**
 * @brief 便捷接口：从一段 Annex B 裸流中自动检索第一个 SPS 并解析
 */
inline bool ParseSpsFromAnnexB(const uint8_t* stream_data, size_t stream_size, SpsInfo& sps) {
    std::vector<Nalu> nalus = SplitNalu(stream_data, stream_size);
    for (const auto& nalu : nalus) {
        if (nalu.type == 7) {
            return ParseSpsFromNalu(nalu.data, nalu.size, sps);
        }
    }
    return false;
}

} // namespace h264
