#include <iostream>
#include <cassert>
#include <vector>
#include "h264_sps_parser.h"

void TestExpGolomb() {
    std::cout << "[Test 1] Testing 0-th Order Exp-Golomb Decoder..." << std::endl;

    // 二进制流: 
    // 1        -> ue = 0
    // 010      -> ue = 1
    // 011      -> ue = 2
    // 00100    -> ue = 3
    // 00101    -> ue = 4
    // 拼装字节流: 1_010_011_0 0100_0010 1_0000000 -> 0xA6, 0x42, 0x80
    uint8_t bits[] = { 0xA6, 0x42, 0x80 };
    h264::BitReader reader(bits, sizeof(bits));

    assert(h264::ReadUe(reader) == 0);
    assert(h264::ReadUe(reader) == 1);
    assert(h264::ReadUe(reader) == 2);
    assert(h264::ReadUe(reader) == 3);
    assert(h264::ReadUe(reader) == 4);

    std::cout << "  -> Passed: Exp-Golomb decoded 0, 1, 2, 3, 4 correctly!" << std::endl;
}

void TestEmulationPrevention() {
    std::cout << "[Test 2] Testing Emulation Prevention (0x03 Removal)..." << std::endl;

    // 包含 0x00, 0x00, 0x03, 0x01 与 0x00, 0x00, 0x03, 0x02
    std::vector<uint8_t> ebsp = { 0xAA, 0x00, 0x00, 0x03, 0x01, 0xBB, 0x00, 0x00, 0x03, 0x02, 0xCC };
    std::vector<uint8_t> expected_rbsp = { 0xAA, 0x00, 0x00, 0x01, 0xBB, 0x00, 0x00, 0x02, 0xCC };

    auto rbsp = h264::RemoveEmulationPreventionBytes(ebsp.data(), ebsp.size());
    assert(rbsp == expected_rbsp);

    std::cout << "  -> Passed: 0x03 successfully stripped, size reduced from " 
              << ebsp.size() << " to " << rbsp.size() << " bytes!" << std::endl;
}

void Test1080pSps() {
    std::cout << "[Test 3] Testing 1080p SPS with Cropping & Emulation Bytes..." << std::endl;

    // 真实的 1920x1080 (High Profile, Level 4.0) SPS NALU
    // 包含 0x03 防竞争字节与 1088 宏块高裁切到 1080 的 cropping 字段
    const uint8_t sps_1080p[] = {
        0x67, 0x64, 0x00, 0x28, 0xac, 0xd9, 0x40, 0x78, 0x02, 0x27, 0xe5, 0xc0,
        0x44, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x3c,
        0x60, 0xc6, 0x58
    };

    h264::SpsInfo sps;
    bool success = h264::ParseSpsFromNalu(sps_1080p, sizeof(sps_1080p), sps);

    assert(success);
    std::cout << "  Profile IDC : " << sps.profile_idc << " (Expected: 100 [High Profile])" << std::endl;
    std::cout << "  Level IDC   : " << sps.level_idc << " (Expected: 40 [Level 4.0])" << std::endl;
    std::cout << "  Width       : " << sps.width << " (Expected: 1920)" << std::endl;
    std::cout << "  Height      : " << sps.height << " (Expected: 1080, cropped from 1088)" << std::endl;
    std::cout << "  Crop Bottom : " << sps.crop_bottom << " (4 macroblock units = 8 pixels)" << std::endl;

    assert(sps.profile_idc == 100);
    assert(sps.level_idc == 40);
    assert(sps.width == 1920);
    assert(sps.height == 1080);
    assert(sps.crop_bottom == 4);

    std::cout << "  -> Passed: 1080p SPS 100% matched!" << std::endl;
}

void Test720pAnnexBStream() {
    std::cout << "[Test 4] Testing Annex B Stream Detection & 720p SPS..." << std::endl;

    // 模拟一段包含起始码 00 00 00 01 的 Annex B 码流
    std::vector<uint8_t> annex_b = {
        // 4 字节起始码 + 720p SPS (Main Profile, Level 3.1)
        0x00, 0x00, 0x00, 0x01,
        0x67, 0x4d, 0x40, 0x1f, 0xec, 0xa0, 0x28, 0x02, 0xdd, 0x80, 0xb5, 0x01,
        0x01, 0x01, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0c, 0x83,
        0xc6, 0x0c, 0x44,
        // 3 字节起始码 + PPS (Type 8)
        0x00, 0x00, 0x01,
        0x68, 0xce, 0x3c, 0x80
    };

    h264::SpsInfo sps;
    bool success = h264::ParseSpsFromAnnexB(annex_b.data(), annex_b.size(), sps);

    assert(success);
    std::cout << "  Profile IDC : " << sps.profile_idc << " (Expected: 77 [Main Profile])" << std::endl;
    std::cout << "  Level IDC   : " << sps.level_idc << " (Expected: 31 [Level 3.1])" << std::endl;
    std::cout << "  Width       : " << sps.width << " (Expected: 1280)" << std::endl;
    std::cout << "  Height      : " << sps.height << " (Expected: 720)" << std::endl;

    assert(sps.profile_idc == 77);
    assert(sps.level_idc == 31);
    assert(sps.width == 1280);
    assert(sps.height == 720);

    std::cout << "  -> Passed: Annex B 720p SPS 100% matched!" << std::endl;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "   H.264 SPS & Annex B Parser Unit Test Suite           " << std::endl;
    std::cout << "========================================================" << std::endl;

    TestExpGolomb();
    TestEmulationPrevention();
    Test1080pSps();
    Test720pAnnexBStream();

    std::cout << "========================================================" << std::endl;
    std::cout << "   ALL 4 TEST SUITES PASSED PERFECTLY! (100% SUCCESS)   " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
