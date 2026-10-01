/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#undef LOG_TAG
//#define LOG_NDEBUG 0
#define LOG_TAG "tsmux_hal"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <ion/ion.h>
#include <arpa/inet.h>

#include "exynos_format.h"

#include "tsmux_hal.h"

#define MAX_HEAP_NAME 32

struct ion_heap_data {
    char name[MAX_HEAP_NAME];
    __u32 type;
    __u32 heap_id;
    __u32 size;       /* reserved 0 */
    __u32 heap_flags; /* reserved 1 */
    __u32 reserved2;
};

namespace android {

#define TSMUX_DEV_NAME       "/dev/tsmux"

#define TS_PID_PMT          0x100
#define TS_PID_PCR          0x1000

#define TS_AVC_STREAM_TYPE  0x1B
#define TS_HEVC_STREAM_TYPE 0x24
#define TS_VIDEO_STREAM_ID  0xE0
#define TS_VIDEO_PACKET_ID  0x1011

#define TS_LPCM_STREAM_TYPE 0x83
#define TS_LPCM_STREAM_ID   0xBD

#define TS_AAC_STREAM_TYPE  0x0F
#define TS_AAC_STREAM_ID    0xC0
#define TS_AUDIO_PACKET_ID  0x1100

#define VIDEO_PROFILE_IDC       66
#define VIDEO_LEVEL_IDC         40
#define VIDEO_CONSTRAINT_SET    192
#define M2M_BUF_SIZE            32768

struct tsmux_hal {
    struct tsmux_m2m_cmd_queue m2m_cmd_queue;
    struct tsmux_otf_cmd_queue otf_cmd_queue;
    struct tsmux_psi_info psi_info;
    void *inbuf_addr[TSMUX_MAX_M2M_CMD_QUEUE_NUM];
    void *outbuf_addr[TSMUX_MAX_M2M_CMD_QUEUE_NUM];
    void *otfbuf_addr[TSMUX_OUT_BUF_CNT];
    int tsmux_fd;
    int ion_client_fd;
    unsigned int heap_id;

    int64_t last_psi_time_us;
    int64_t audio_frame_count;
    int64_t video_frame_count;

    struct tsmux_rtp_ts_info rtp_ts_info;

    uint32_t crc_table[256];
    bool use_hevc;
    bool use_lpcm;
};

void depacketize_rtp(char *ts_data, int *ts_size, char *rtp_data, int rtp_size)
{
    char *rtp_ptr, *ts_ptr;
    rtp_ptr = rtp_data;
    *ts_size = 0;
    int ts_packet_size = 188;
    while (rtp_size > 0) {

        rtp_ptr += 1; /* skip ver(2b), padding(1b), extenstion(2b), CSCR Count(4b) */
        rtp_ptr += 1; /* skip Marker(1b), payload type(7b) */
        rtp_ptr += 2; /* skip sequence number(16b) */
        rtp_ptr += 4; /* skip timestamp(32b) */
        rtp_ptr += 4; /* skip SSRC(32b) */
        rtp_size -= 12;

        ts_ptr = rtp_ptr;
        int remain_ts_packet = TS_PKT_COUNT_PER_RTP;
        while (remain_ts_packet > 0) {
            memcpy(ts_data, ts_ptr, ts_packet_size);
            ts_data += ts_packet_size;
            ts_ptr += ts_packet_size;
            rtp_size -= ts_packet_size;
            *ts_size += ts_packet_size;
            remain_ts_packet -= 1;
            if (rtp_size == 0)
                break;
        }
        rtp_ptr = ts_ptr;
    }
}

void depacketize(char* depacketized_data, char* packetized_data, int es_size, bool psi)
{
    char *rtp_ptr, *ts_ptr, *pes_ptr, *es_ptr;
    char *out_data_ptr = depacketized_data;
    char adaptation_field_control;
    int adaptation_field_length;
    int is_pes_header = 1;
    int remain_es_size = es_size;
    int psi_packet = 3;

    rtp_ptr = packetized_data;
    while (remain_es_size > 0) {

        rtp_ptr += 1; /* skip ver(2b), padding(1b), extenstion(2b), CSCR Count(4b) */
        rtp_ptr += 1; /* skip Marker(1b), payload type(7b) */
        rtp_ptr += 2; /* skip sequence number(16b) */
        rtp_ptr += 4; /* skip timestamp(32b) */
        rtp_ptr += 4; /* skip SSRC(32b) */

        ts_ptr = rtp_ptr;
        int remain_ts_packet = 7;
        while (remain_ts_packet > 0) {
            ts_ptr += 1; /* skip sync byte(8b) */
            ts_ptr += 2; /* skip err(1b), start(1b), priority(1b), PID(13b) */
            adaptation_field_control = ((*ts_ptr) >> 4) & 0x3;
            ALOGV("depacketize(), adaptation_field_control 0x%x", adaptation_field_control);
            ts_ptr += 1; /* skip scarmbling(1b), adaptation(2b), continuity counter(2b) */
            if (adaptation_field_control == 0x3) {
                adaptation_field_length = *ts_ptr;
                ts_ptr += 1;
            }

            int ts_payload = 184;

            if (psi && psi_packet > 0) {
                ts_ptr += 184;
                psi_packet--;
            } else {
                pes_ptr = ts_ptr;
                if (is_pes_header) {
                    pes_ptr += 3; /* skip start code */
                    pes_ptr += 1; /* skip stream id */
                    pes_ptr += 2; /* skip PES packet length */
                    pes_ptr += 1; /* skip scramble, priority, coptyright, copy */
                    pes_ptr += 1; /* skip PTS DTS flag, PES extention flag */
                    pes_ptr += 1; /* skip PES header data length */
                    pes_ptr += 5; /* skip PTS */
                    is_pes_header = 0;
                    ts_payload -= 14;
                }

                es_ptr = pes_ptr;
                if (adaptation_field_control == 0x3) {
                    es_ptr += adaptation_field_length;
                    ALOGV("depacketize(), adaptation_field_length %d, %.2x %.2x %.2x %.2x",
                        adaptation_field_length, *(es_ptr), *(es_ptr + 1), *(es_ptr + 2), *(es_ptr + 3));
                }
                int copy_size;
                if (remain_es_size >= ts_payload) {
                    copy_size = ts_payload;
                } else {
                    copy_size = remain_es_size;
                }
                memcpy(out_data_ptr, es_ptr, copy_size);
                out_data_ptr += copy_size;
                remain_es_size -= copy_size;
                ts_ptr = es_ptr + copy_size;
            }

            remain_ts_packet -= 1;
            if (remain_es_size == 0)
                break;
        }
        rtp_ptr = ts_ptr;
    }
}

int increament_ts_continuity_counter(int ts_continuity_counter, int rtp_size, int psi_enable)
{
#if 0
    // len_pes
    int len_es = (int)cur_out_buf->es_size;
    int flg_enc = 0;
    int num_pad = 0;    // video is 0, audio is 2
    int len_hdr = 5 + pkt_ctrl->pes_stuffing_num;
    int len_pes = len_es + 14 + pkt_ctrl->pes_stuffing_num;

    // len_tsp
    int flg_psi = pkt_ctrl->psi_en;
    int len_tmp = len_pes - len_hdr - (184 - len_hdr) / 16 * 16;
    len_tmp = len_es <= (184 - len_hdr) ? 188 : 188 + (len_tmp / 176 * 188) +
        ((len_tmp % 176 > 8) || (len_tmp > 0 && len_tmp <= 8) ? 188 : 0);
    int len_tsp = (flg_psi ? 188 * 3 : 0) + (flg_enc ? len_tmp : (len_pes + 183) / 184 * 188);

    // len_rtp
    int len_rtp = ((len_tsp + 7 * 188 - 1) / (7 * 188)) * 12 + len_tsp;

    if (pkt_ctrl->psi_en)
        hal->ts_continuity_counter += (len_tsp - 188 * 3) / 188;
    else
        hal->ts_continuity_counter += len_tsp / 188;
    hal->ts_continuity_counter %= 16;

    ALOGV("len_es %d, len_pes %d, len_tsp %d, len_rtp %d, ts_continuity_counter %d",
        len_es, len_pes, len_tsp, len_rtp, hal->ts_continuity_counter);
#endif

    int rtp_packet_count = rtp_size / (188 * TS_PKT_COUNT_PER_RTP + 12);
    int ts_packet_count = rtp_packet_count * TS_PKT_COUNT_PER_RTP;
    int rtp_remain_size = rtp_size % (188 * TS_PKT_COUNT_PER_RTP + 12);
    if (rtp_remain_size > 0) {
        int ts_ramain_size = rtp_remain_size - 12;
        ts_packet_count += ts_ramain_size / 188;
    }
    if (psi_enable) {
        ts_packet_count -= 3;
    }

    int new_ts_continuity_counter = ts_continuity_counter + ts_packet_count;
    new_ts_continuity_counter = new_ts_continuity_counter & 0xf;
    ALOGV("increament_ts_continuity_counter(), rtp_size %d, ts_packet_count %d, ts_continuity_counter %d, new_ts_continuity_counter %d",
        rtp_size, ts_packet_count, ts_continuity_counter, new_ts_continuity_counter);

    return new_ts_continuity_counter;
}

int get_rtp_size(int es_size, bool audio, bool psi_enable, bool hdcp_enable)
{
    // len_pes
    int len_es = es_size;
    int flg_enc = hdcp_enable;
    int pes_stuffing_num;
    if (audio)
        pes_stuffing_num = 2;
    else
        pes_stuffing_num = 0;
    int len_hdr = 5 + pes_stuffing_num;
    int len_pes = len_es + 14 + pes_stuffing_num;

    // len_tsp
    int flg_psi = psi_enable;
    int len_tmp = len_pes - len_hdr - (184 - len_hdr) / 16 * 16;
    len_tmp = len_es <= (184 - len_hdr) ? 188 : 188 + (len_tmp / 176 * 188) +
        ((len_tmp % 176 > 8) || (len_tmp > 0 && len_tmp <= 8) ? 188 : 0);
    int len_tsp = (flg_psi ? 188 * 3 : 0) + (flg_enc ? len_tmp : (len_pes + 183) / 184 * 188);

    // len_rtp
    int len_rtp = ((len_tsp + 7 * 188 - 1) / (7 * 188)) * 12 + len_tsp;

    ALOGV("tsmux_get_rtp_size(), audio %d, psi_enable %d len_es %d, len_pes %d, len_tsp %d, len_rtp %d",
        audio, psi_enable, len_es, len_pes, len_tsp, len_rtp);

    return len_rtp;
}

void addADTSHeader(uint8_t *dst, uint8_t *src, int es_size,
    int profile, int sampling_freq_index, int channel_configuration) {
    const uint32_t aac_frame_length = es_size + 7;

    uint8_t *ptr = dst;

    *ptr++ = 0xff;
    *ptr++ = 0xf9;  // b11111001, ID=1(MPEG-2), layer=0, protection_absent=1

    *ptr++ =
        profile << 6
        | sampling_freq_index << 2
        | ((channel_configuration >> 2) & 1);  // private_bit=0

    // original_copy=0, home=0, copyright_id_bit=0, copyright_id_start=0
    *ptr++ =
        (channel_configuration & 3) << 6
        | aac_frame_length >> 11;
    *ptr++ = (aac_frame_length >> 3) & 0xff;
    *ptr++ = (aac_frame_length & 7) << 5;

    // adts_buffer_fullness=0, number_of_raw_data_blocks_in_frame=0
    *ptr++ = 0;

    memcpy(ptr, src, es_size);

    uint8_t *temp_ptr = src;
    ALOGV("addADTSHeader(), src %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x",
        *(temp_ptr), *(temp_ptr + 1), *(temp_ptr + 2), *(temp_ptr + 3),
        *(temp_ptr + 4), *(temp_ptr + 5), *(temp_ptr + 6), *(temp_ptr + 7),
        *(temp_ptr + 8), *(temp_ptr + 9), *(temp_ptr + 10), *(temp_ptr + 11));

    temp_ptr = dst;
    ALOGV("addADTSHeader(), dst %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x",
        *(temp_ptr), *(temp_ptr + 1), *(temp_ptr + 2), *(temp_ptr + 3),
        *(temp_ptr + 4), *(temp_ptr + 5), *(temp_ptr + 6), *(temp_ptr + 7),
        *(temp_ptr + 8), *(temp_ptr + 9), *(temp_ptr + 10), *(temp_ptr + 11));
}

void tsmux_init_crc_table(void *handle) {
    struct tsmux_hal *hal;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    uint32_t poly = 0x04C11DB7;

    for (int i = 0; i < 256; i++) {
        uint32_t crc = i << 24;
        for (int j = 0; j < 8; j++) {
            crc = (crc << 1) ^ ((crc & 0x80000000) ? (poly) : 0);
        }
        hal->crc_table[i] = crc;
    }

    for (int i = 0; i < 256; i += 8) {
        ALOGV("hal->crc_table 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
            hal->crc_table[i], hal->crc_table[i + 1], hal->crc_table[i + 2], hal->crc_table[i + 3],
            hal->crc_table[i + 4], hal->crc_table[i + 5], hal->crc_table[i + 6], hal->crc_table[i + 7]);
    }

}

static uint32_t tsmux_crc32(void *handle, const uint8_t *start, size_t size) {
    struct tsmux_hal *hal;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return 0;
    }

    hal = (struct tsmux_hal *)handle;

    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p;

    for (p = start; p < start + size; ++p) {
        crc = (crc << 8) ^ hal->crc_table[((crc >> 24) ^ *p) & 0xFF];
    }

    return crc;
}

void tsmux_send_psi(void *handle)
{
    int ret;
    struct tsmux_hal *hal;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    ALOGV("send_psi");
    uint8_t *packetDataStart = (uint8_t *)hal->psi_info.psi_data;

    /* PAT */
    /* PAT PID is 0 */
    uint8_t *ptr = packetDataStart;
    *ptr++ = 0x47;
    *ptr++ = 0x40;
    *ptr++ = 0x00;
    *ptr++ = 0x10; /* PAT CC will be set by tsmux device driver */
    *ptr++ = 0x00;

    uint8_t *crcDataStart = ptr;
    *ptr++ = 0x00;
    *ptr++ = 0xb0;
    *ptr++ = 0x0d;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0xc3;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x01;
    *ptr++ = 0xe0 | (TS_PID_PMT >> 8);
    *ptr++ = TS_PID_PMT & 0xff;

    uint32_t crc = htonl(tsmux_crc32(hal, crcDataStart, ptr - crcDataStart));
    ALOGV("pat crc 0x%x", crc);
    memcpy(ptr, &crc, 4);
    ptr += 4;

    hal->psi_info.pat_len = ptr - packetDataStart;

    /* PMT */
    packetDataStart = ptr;
    *ptr++ = 0x47;
    *ptr++ = 0x40 | (TS_PID_PMT >> 8);
    *ptr++ = TS_PID_PMT & 0xff;
    *ptr++ = 0x10;  /* PMT CC will be set by tsmux device driver */
    *ptr++ = 0x00;

    crcDataStart = ptr;
    *ptr++ = 0x02;

    *ptr++ = 0x00;  // section_length to be filled in below.
    *ptr++ = 0x00;

    *ptr++ = 0x00;
    *ptr++ = 0x01;
    *ptr++ = 0xc3;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0xe0 | (TS_PID_PCR >> 8);
    *ptr++ = TS_PID_PCR & 0xff;

    if (hal->otf_cmd_queue.config.hex_ctrl.otf_enable) {
        // there is only one prgram descriptor
        // HDCP descriptor
        size_t program_info_length = 7;
        *ptr++ = 0xf0 | (program_info_length >> 8);
        *ptr++ = (program_info_length & 0xff);
        *ptr++ = 0x05;  // descriptor_tag
        *ptr++ = 5;  // descriptor_length
        *ptr++ = 'H';
        *ptr++ = 'D';
        *ptr++ = 'C';
        *ptr++ = 'P';
        *ptr++ = 0x20; //hdcpVersion
    } else {
        size_t program_info_length = 0;
        *ptr++ = 0xf0 | (program_info_length >> 8);
        *ptr++ = (program_info_length & 0xff);
    }

    // video
    size_t ES_info_length;
    if (hal->use_hevc) {
        *ptr++ = TS_HEVC_STREAM_TYPE;
        *ptr++ = 0xe0 | (TS_VIDEO_PACKET_ID >> 8);
        *ptr++ = TS_VIDEO_PACKET_ID & 0xff;
        ES_info_length = 19;
    } else {
        *ptr++ = TS_AVC_STREAM_TYPE;
        *ptr++ = 0xe0 | (TS_VIDEO_PACKET_ID >> 8);
        *ptr++ = TS_VIDEO_PACKET_ID & 0xff;
        ES_info_length = 10;
    }
    *ptr++ = 0xf0 | (ES_info_length >> 8);
    *ptr++ = (ES_info_length & 0xff);

    if (hal->use_hevc) {
        // HEVC video descriptor
        *ptr++ = 56;
        *ptr++ = 13;
        *ptr++ = 1;
        *ptr++ = (0x60000000 >> 24) & 0xff;
        *ptr++ = (0x60000000 >> 16) & 0xff;
        *ptr++ = (0x60000000 >> 8) & 0xff;
        *ptr++ = 0x60000000 & 0xff;
        *ptr++ = ((1 << 7) | (1 << 5) | (1 << 4)) & 0xf0;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = 120;
        *ptr++ = 0;

        // HEVC timing and HRD descriptor
        *ptr++ = 63;
        *ptr++ = 2;
        *ptr++ = 1;
        *ptr++ = 0x7e;
    } else {
        // AVC video descriptor (40)
        *ptr++ = 40;                    // descriptor_tag
        *ptr++ = 4;                     // descriptor_length
        *ptr++ = VIDEO_PROFILE_IDC;     // profile_idc
        *ptr++ = VIDEO_CONSTRAINT_SET;  // constraint_set*
        *ptr++ = VIDEO_LEVEL_IDC;       // level_idc
        *ptr++ = 0x3f;

        // AVC timing and HRD descriptor (42)
        *ptr++ = 42;  // descriptor_tag
        *ptr++ = 2;  // descriptor_length
        // hrd_management_valid_flag = 0
        // reserved = 111111b
        // picture_and_timing_info_present = 0
        *ptr++ = 0x7e;

        // fixed_frame_rate_flag = 0
        // temporal_poc_flag = 0
        // picture_to_display_conversion_flag = 0
        // reserved = 11111b
        *ptr++ = 0x1f;
    }

    //audio
    if (hal->use_lpcm) {
        *ptr++ = TS_LPCM_STREAM_TYPE;
        ES_info_length = 4;
    } else {
        *ptr++ = TS_AAC_STREAM_TYPE;
        ES_info_length = 0;
    }

    *ptr++ = 0xe0 | (TS_AUDIO_PACKET_ID >> 8);
    *ptr++ = TS_AUDIO_PACKET_ID & 0xff;

    *ptr++ = 0xf0 | (ES_info_length >> 8);
    *ptr++ = (ES_info_length & 0xff);

    if (hal->use_lpcm) {
        /* SLSI add lpcm descriptor */
        *ptr++ = TS_LPCM_STREAM_TYPE;   // descriptor_tag
        *ptr++ = 2;                     // descriptor_length
        //int32_t sampleRate = 48000;
        unsigned sampling_frequency = 2;    //(sampleRate == 44100) ? 1 : 2;
        *ptr++ = (sampling_frequency << 5) | (3 /* reserved */ << 1) | 0 /* emphasis_flag */;
        *ptr++ =(1 /* number_of_channels = stereo */ << 5) | 0xf /* reserved */;
    }

    size_t section_length = ptr - (crcDataStart + 3) + 4 /* CRC */;
    crcDataStart[1] = 0xb0 | (section_length >> 8);
    crcDataStart[2] = section_length & 0xff;
    crc = htonl(tsmux_crc32(hal, crcDataStart, ptr - crcDataStart));
    ALOGV("pmt crc 0x%x", crc);
    memcpy(ptr, &crc, 4);
    ptr += 4;

    ALOGV("section_length %d", (int)section_length);

    hal->psi_info.pmt_len = ptr - packetDataStart;

    /* PCR */
    /* PCR should be set by tsmux device driver */
    packetDataStart = ptr;
    int64_t nowUs = systemTime(SYSTEM_TIME_MONOTONIC) / 1000ll;
    uint64_t PCR = nowUs * 27;  // PCR based on a 27MHz clock
    uint64_t PCR_base = PCR / 300;
    uint32_t PCR_ext = PCR % 300;

    *ptr++ = 0x47;
    *ptr++ = 0x40 | (TS_PID_PCR >> 8);
    *ptr++ = TS_PID_PCR & 0xff;
    *ptr++ = 0x20;
    *ptr++ = 0xb7;  // adaptation_field_length
    *ptr++ = 0x10;

    *ptr++ = (PCR_base >> 25) & 0xff;
    *ptr++ = (PCR_base >> 17) & 0xff;
    *ptr++ = (PCR_base >> 9) & 0xff;
    *ptr++ = ((PCR_base & 1) << 7) | 0x7e | ((PCR_ext >> 8) & 1);
    *ptr++ = (PCR_ext & 0xff);

    hal->psi_info.pcr_len = ptr - packetDataStart;

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_SET_INFO, &hal->psi_info);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_SET_INFO");
    }

    ALOGV("len pat %d, pmt %d, pcr %d",
        hal->psi_info.pat_len,
        hal->psi_info.pmt_len,
        hal->psi_info.pcr_len);
}

void *tsmux_open(bool enable_hdcp, bool use_hevc, bool use_lpcm)
{
    struct tsmux_hal *hal;
    int i;

    hal = (struct tsmux_hal *)malloc(sizeof(struct tsmux_hal));

    hal->tsmux_fd = open(TSMUX_DEV_NAME, O_RDWR);
    if (hal->tsmux_fd < 0)
    {
        ALOGE("%s: open fail tsmux module (%s)", __FUNCTION__, TSMUX_DEV_NAME);
        free(hal);
        return NULL;
    }

    for (i = 0; i < TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd = -1;
        hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd = -1;
        hal->inbuf_addr[i] = NULL;
        hal->outbuf_addr[i] = NULL;
    }

    for (i = 0; i < TSMUX_OUT_BUF_CNT; i++) {
        hal->otf_cmd_queue.out_buf[i].ion_buf_fd = -1;
        hal->otfbuf_addr[i] = NULL;
    }

    hal->ion_client_fd = ion_open();
    if (hal->ion_client_fd < 0) {
        ALOGE("Failed to open /dev/ion");
        free(hal);
        return NULL;
    }

    hal->heap_id = 0;
    if (!ion_is_legacy(hal->ion_client_fd)) {
        int heap_cnt = 0;

        if (ion_query_heap_cnt(hal->ion_client_fd, &heap_cnt) < 0 || heap_cnt <= 0) {
            ALOGE("fail to query the heap count. heap_cnt: %d", heap_cnt);
            free(hal);
            return NULL;
        }

        struct ion_heap_data heaps[heap_cnt];

        if (ion_query_get_heaps(hal->ion_client_fd, heap_cnt, heaps) < 0) {
            ALOGE("fail to query the heaps");
            free(hal);
            return NULL;
        }

        for (int i = 0; i < heap_cnt; i++) {
            if (strcmp(heaps[i].name, "ion_system_heap") == 0)
                hal->heap_id = heaps[i].heap_id;
        }
    }

    ALOGI("tsmux heap_id %d", hal->heap_id);

    hal->last_psi_time_us = 0;

    tsmux_init_crc_table(hal);

    hal->otf_cmd_queue.config.hex_ctrl.otf_enable = enable_hdcp ? 1 : 0;
    hal->otf_cmd_queue.config.hex_ctrl.m2m_enable = 0;
    hal->use_hevc = use_hevc;
    hal->use_lpcm = use_lpcm;
    ALOGI("tsmux opened, enable_hdcp: %d, use_hevc: %d, use_lpcm: %d", enable_hdcp, use_hevc, use_lpcm);

    return hal;
}

void tsmux_close(void *handle)
{
    struct tsmux_hal *hal;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    if (hal->ion_client_fd > 0) {
        ion_close(hal->ion_client_fd);
        hal->ion_client_fd = -1;
    }

    if (hal->tsmux_fd > 0) {
        close(hal->tsmux_fd);
        hal->tsmux_fd = -1;
    }

    free(hal);

    ALOGI("tsmux_close");
}

void tsmux_get_switching_info(void *handle, int *rtpSeqNum,
    int *patCC, int *pmtCC, int *videoCC, int *audioCC)
{
    struct tsmux_hal *hal;
    int ret;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_GET_RTP_TS_INFO, &hal->rtp_ts_info);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_GET_RTP_TS_INFO");
        return;
    }

    *patCC = hal->rtp_ts_info.ts_pat_cc;
    *pmtCC = hal->rtp_ts_info.ts_pmt_cc;
    *rtpSeqNum = hal->rtp_ts_info.rtp_seq_number;
    *videoCC = hal->rtp_ts_info.ts_video_cc;
    *audioCC = hal->rtp_ts_info.ts_audio_cc;

    ALOGI("tsmux_get_switching_info(), ioctl(), rtpSeqNum 0x%x, patCC 0x%x, pmtCC 0x%x, videoCC 0x%x, audioCC 0x%x",
        *rtpSeqNum, *patCC, *pmtCC, *videoCC, *audioCC);
}

void tsmux_set_switching_info(void *handle, int rtpSeqNum,
    int patCC, int pmtCC, int videoCC, int audioCC)
{
    struct tsmux_hal *hal;
    int ret;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    ALOGI("tsmux_set_switching_info(), rtpSeqNum 0x%x, patCC 0x%x, pmtCC 0x%x, videoCC 0x%x, audioCC 0x%x",
        rtpSeqNum, patCC, pmtCC, videoCC, audioCC);

    hal->rtp_ts_info.ts_pat_cc = patCC;
    hal->rtp_ts_info.ts_pmt_cc = pmtCC;
    hal->rtp_ts_info.rtp_seq_number = rtpSeqNum;
    hal->rtp_ts_info.rtp_seq_override = 1;
    hal->rtp_ts_info.ts_video_cc = videoCC;
    hal->rtp_ts_info.ts_audio_cc = audioCC;

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_SET_RTP_TS_INFO, &hal->rtp_ts_info);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_SET_RTP_TS_INFO");
        return;
    }
}

int tsmux_init_m2m(void *handle)
{
    int ret;
    struct tsmux_hal *hal;
    int i;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return -ENOENT;
    }

    hal = (struct tsmux_hal *)handle;

    /* ion free */
    for (i = 0; i < TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        if (hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd > 0)
            close(hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd);
        if (hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd > 0)
            close(hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd);
    }

    /* init m2m job */
    for (i = 0; i < TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        hal->m2m_cmd_queue.m2m_job[i].in_buf.offset = 0;
        hal->m2m_cmd_queue.m2m_job[i].in_buf.actual_size = 0;
        hal->m2m_cmd_queue.m2m_job[i].in_buf.buffer_size = M2M_BUF_SIZE;

        hal->m2m_cmd_queue.m2m_job[i].out_buf.offset = 0;
        hal->m2m_cmd_queue.m2m_job[i].out_buf.actual_size = 0;
        hal->m2m_cmd_queue.m2m_job[i].out_buf.buffer_size = M2M_BUF_SIZE;

        hal->m2m_cmd_queue.m2m_job[i].hex_ctrl.m2m_enable = 0;
        hal->m2m_cmd_queue.m2m_job[i].hex_ctrl.otf_enable = 0;
    }

    /* ion alloc */
    for (i = 0; i < TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        ret = ion_alloc_fd(hal->ion_client_fd, M2M_BUF_SIZE, 0, 1 << hal->heap_id, 0,
                &hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd);
        if (ret < 0) {
            ALOGE("ion_alloc_fd fail");
            return ret;
        } else {
            hal->inbuf_addr[i] = mmap(0, M2M_BUF_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED,
                    hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd, 0);
            if (hal->inbuf_addr[i] == MAP_FAILED) {
                ALOGE("mmap fail");
                close(hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd);
                return -ENOMEM;
            }
        }

    }

    for (i = 0; i < TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        ret = ion_alloc_fd(hal->ion_client_fd, M2M_BUF_SIZE, 0, 1 << hal->heap_id, 0,
                &hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd);
        if (ret < 0) {
            ALOGE("ion_alloc_fd fail");
            return ret;
        } else {
            hal->outbuf_addr[i] = mmap(0, M2M_BUF_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED,
                    hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd, 0);
            if (hal->outbuf_addr[i] == MAP_FAILED) {
                ALOGE("mmap fail");
                close(hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd);
                return -ENOMEM;
            }
        }
    }

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_M2M_MAP_BUF, &hal->m2m_cmd_queue);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_M2M_MAP_BUF");
        return ret;
    }

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_SET_INFO, &hal->psi_info);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_SET_INFO");
        return ret;
    }

    return ret;
}

void tsmux_deinit_m2m(void *handle)
{
    struct tsmux_hal *hal;
    int i;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    ioctl(hal->tsmux_fd, TSMUX_IOCTL_M2M_UNMAP_BUF);

    /* ion free */
    for (i = 0; i < TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        if (hal->inbuf_addr[i] != NULL) {
            munmap(hal->inbuf_addr[i], hal->m2m_cmd_queue.m2m_job[i].in_buf.buffer_size);
            hal->inbuf_addr[i] = NULL;
        }

        if (hal->outbuf_addr[i] != NULL) {
            munmap(hal->outbuf_addr[i], hal->m2m_cmd_queue.m2m_job[i].out_buf.buffer_size);
            hal->outbuf_addr[i] = NULL;
        }

        if (hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd > 0)
            close(hal->m2m_cmd_queue.m2m_job[i].in_buf.ion_buf_fd);
        if (hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd > 0)
            close(hal->m2m_cmd_queue.m2m_job[i].out_buf.ion_buf_fd);
    }

    ALOGI("tsmux_deinit_m2m");
}

int tsmux_packetize_m2m(void *handle, sp<ABuffer> *inbufs, sp<ABuffer> *outbufs)
{
    int ret;
    struct tsmux_hal *hal;
    int64_t timeUs[TSMUX_MAX_M2M_CMD_QUEUE_NUM] = {0};
    int i;
    struct tsmux_pkt_ctrl *pkt_ctrl;
    struct tsmux_pes_hdr *pes_hdr;
    struct tsmux_ts_hdr *ts_hdr;
    struct tsmux_rtp_hdr *rtp_hdr;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return -ENOENT;
    }

    hal = (struct tsmux_hal *)handle;

    // init
    for (i = 0; i <TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        pes_hdr = &hal->m2m_cmd_queue.m2m_job[i].pes_hdr;
        pes_hdr->pts39_16 = -1;
    }

    for (i = 0; i <TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        if (inbufs[i] == NULL)
            continue;

        CHECK(inbufs[i]->meta()->findInt64("timeUs", &timeUs[i]));

        int inbuf_size = inbufs[i]->size();
        ALOGV("tsmux_packetize_m2m(), i %d, inbufs %d", i, inbuf_size);

        if (hal->use_lpcm)
            memcpy((uint8_t *)hal->inbuf_addr[i], (uint8_t *)inbufs[i]->data(), inbuf_size);
        else {
            addADTSHeader((uint8_t *)hal->inbuf_addr[i], (uint8_t *)inbufs[i]->data(),
                inbuf_size, 1/* AAC_LC */, 3/* 48000Hz */, 2 /* 2 channels */);
            inbuf_size += 7;
        }

        hal->m2m_cmd_queue.m2m_job[i].in_buf.actual_size = inbuf_size;

        pkt_ctrl = &hal->m2m_cmd_queue.m2m_job[i].pkt_ctrl;

        int64_t nowUs = systemTime(SYSTEM_TIME_MONOTONIC) / 1000ll;
        if (nowUs - hal->last_psi_time_us > 50000) {
            hal->last_psi_time_us = nowUs;
            tsmux_send_psi(handle);
            pkt_ctrl->psi_en = 1;
        } else {
            pkt_ctrl->psi_en = 0;
        }

        pkt_ctrl->pes_stuffing_num = 2;  // video 0, audio 2
        pkt_ctrl->mode = 0;              // m2m 0, otf 1
        pkt_ctrl->id = i + 1;            // m2m 1 ~ 3, otf 0

        pes_hdr = &hal->m2m_cmd_queue.m2m_job[i].pes_hdr;
        pes_hdr->code = PES_HDR_CODE;
        if(hal->use_lpcm) {
            pes_hdr->stream_id = TS_LPCM_STREAM_ID;
        } else {
            pes_hdr->stream_id = TS_AAC_STREAM_ID;
        }
        pes_hdr->pkt_len = inbuf_size;
        pes_hdr->pkt_len += 3; /* extension, byte 6 ~ 8 */
        pes_hdr->pkt_len += 5; /* PTS, byte 9 ~ 13 */
        pes_hdr->pkt_len += 2; /* stuffing bytes, byte 14 ~ 15 */
        pes_hdr->marker = PES_HDR_MARKER;
        pes_hdr->scramble = 0;
        pes_hdr->priority = 0;
        pes_hdr->alignment = 1;
        pes_hdr->copyright = 0;
        pes_hdr->original = 0;
        pes_hdr->flags = 0;
        if (hal->m2m_cmd_queue.m2m_job[i].hex_ctrl.m2m_enable) {
            pes_hdr->flags = 0x81;
            /* add PES extension flags and PES private data size */
            pes_hdr->hdr_len = 5 + pkt_ctrl->pes_stuffing_num + 17;
        } else {
            pes_hdr->flags = 0x80;
            pes_hdr->hdr_len = 5 + pkt_ctrl->pes_stuffing_num;
        }
        uint64_t PTS = (timeUs[i] * 9ll) / 100ll;
        pes_hdr->pts39_16 = (0x20 | (((PTS >> 30) & 7) << 1) | 1) << 16;
        pes_hdr->pts39_16 |= ((PTS >> 22) & 0xff) << 8;
        pes_hdr->pts39_16 |= ((((PTS >> 15) & 0x7f) << 1) | 1);
        pes_hdr->pts15_0 = ((PTS >> 7) & 0xff) << 8;
        pes_hdr->pts15_0 |= (((PTS & 0x7f) << 1) | 1);
        ALOGV("tsmux_packetize_m2m time_stamp %lld, PTS %lld, pts39_16 0x%x, pts15_0 0x%x",
            (long long)timeUs[i], (long long)PTS, pes_hdr->pts39_16, pes_hdr->pts15_0);

        ts_hdr = &hal->m2m_cmd_queue.m2m_job[i].ts_hdr;
        ts_hdr->sync = TS_HDR_SYNC;
        ts_hdr->error = 0;
        ts_hdr->priority = 0;
        ts_hdr->pid = TS_AUDIO_PACKET_ID;
        ts_hdr->scramble = 0;
        ts_hdr->adapt_ctrl = 0;
        /* TS continuity counter should be set by tsmux device driver */

        rtp_hdr = &hal->m2m_cmd_queue.m2m_job[i].rtp_hdr;
        rtp_hdr->ver = 0x2;
        rtp_hdr->pad = 0;
        rtp_hdr->ext = 0;
        rtp_hdr->csrc_cnt = 0;
        rtp_hdr->marker = 0;
        rtp_hdr->pl_type = 33;

        rtp_hdr->ssrc = 0xdeadbeef;
    }

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_M2M_RUN, &hal->m2m_cmd_queue);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_M2M_RUN");
        return ret;
    }

    for (i = 0; i <TSMUX_MAX_M2M_CMD_QUEUE_NUM; i++) {
        int outbufSize = hal->m2m_cmd_queue.m2m_job[i].out_buf.actual_size;
        pkt_ctrl = &hal->m2m_cmd_queue.m2m_job[i].pkt_ctrl;
        ALOGV("tsmux_packetize_m2m(), i %d, outbufSize %d", i, outbufSize);
        if (outbufSize > 0) {
            outbufs[i] = new ABuffer(outbufSize);
            memcpy(outbufs[i]->data(), hal->outbuf_addr[i],
                hal->m2m_cmd_queue.m2m_job[i].out_buf.actual_size);
            outbufs[i]->meta()->setInt64("timeUs", timeUs[i]);
            hal->audio_frame_count++;
            outbufs[i]->meta()->setInt32("rtp_size", pkt_ctrl->rtp_size);
        }
    }

    return ret;
}

int tsmux_init_otf(void *handle, uint32_t width, uint32_t height) {
    int ret;
    struct tsmux_hal *hal;
    int i;
    unsigned int buffer_size;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return -ENOENT;
    }

    hal = (struct tsmux_hal *)handle;

    /* ion free */
    for (i = 0; i < TSMUX_OUT_BUF_CNT; i++) {
        if (hal->otf_cmd_queue.out_buf[i].ion_buf_fd > 0)
            close(hal->otf_cmd_queue.out_buf[i].ion_buf_fd);
    }

    buffer_size = ALIGN((ALIGN(width, 16) * ALIGN(height, 16) * 3) / 2, 512);

    /* init otf job */
    for (i = 0; i < TSMUX_OUT_BUF_CNT; i++) {
        hal->otf_cmd_queue.out_buf[i].offset = 0;
        hal->otf_cmd_queue.out_buf[i].actual_size = 0;
        hal->otf_cmd_queue.out_buf[i].buffer_size = buffer_size;
    }

    /* ion alloc */
    for (i = 0; i < TSMUX_OUT_BUF_CNT; i++) {
        ret = ion_alloc_fd(hal->ion_client_fd, buffer_size, 0, 1 << hal->heap_id, 0,
                &hal->otf_cmd_queue.out_buf[i].ion_buf_fd);
        if (ret < 0) {
            ALOGE("ion_alloc_fd fail");
            return ret;
        } else {
            hal->otfbuf_addr[i] = mmap(0, buffer_size, PROT_READ|PROT_WRITE, MAP_SHARED,
                hal->otf_cmd_queue.out_buf[i].ion_buf_fd, 0);
            if (hal->otfbuf_addr[i] == MAP_FAILED) {
                ALOGE("mmap fail");
                close(hal->otf_cmd_queue.out_buf[i].ion_buf_fd);
                return -ENOMEM;
            }
        }
    }

    hal->video_frame_count = 0;

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_OTF_MAP_BUF, &hal->otf_cmd_queue);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_OTF_MAP_BUF");
        return ret;
    }

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_SET_INFO, &hal->psi_info);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_SET_INFO");
        return ret;
    }

    ALOGV("tsmux_init_otf: %d", ret);

    return ret;
}

void tsmux_deinit_otf(void *handle) {
    struct tsmux_hal *hal;
    int i;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    ioctl(hal->tsmux_fd, TSMUX_IOCTL_OTF_UNMAP_BUF);

    /* ion free */
    for (i = 0; i < TSMUX_OUT_BUF_CNT; i++) {
        if (hal->otfbuf_addr[i] != NULL) {
            munmap(hal->otfbuf_addr[i],
                    hal->otf_cmd_queue.out_buf[i].buffer_size);
            hal->otfbuf_addr[i] = NULL;
        }

        if (hal->otf_cmd_queue.out_buf[i].ion_buf_fd > 0) {
            close(hal->otf_cmd_queue.out_buf[i].ion_buf_fd);
            hal->otf_cmd_queue.out_buf[i].ion_buf_fd = -1;
        }
    }

    ALOGI("tsmux_deinit_otf");
}

int tsmux_dq_buf_otf(void *handle, sp<ABuffer> &outbuf)
{
    int ret;
    struct tsmux_hal *hal;
    int cur_buf_index;
    struct tsmux_buffer *cur_out_buf = NULL;

    ALOGV("tsmux_dq_buf_otf()");

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return -1;
    }

    hal = (struct tsmux_hal *)handle;

    struct tsmux_pkt_ctrl *pkt_ctrl = &hal->otf_cmd_queue.config.pkt_ctrl;

    int64_t nowUs = systemTime(SYSTEM_TIME_MONOTONIC) / 1000ll;
    if (nowUs - hal->last_psi_time_us > 50000) {
        hal->last_psi_time_us = nowUs;
        tsmux_send_psi(handle);
        pkt_ctrl->psi_en = 1;
    } else {
        pkt_ctrl->psi_en = 0;
    }

    pkt_ctrl->pes_stuffing_num = 0;  // video 0, audio 2
    pkt_ctrl->mode = 1;              // m2m 0, otf 1
    pkt_ctrl->id = 0;                // m2m 1 ~ 3, otf 0
    ALOGV("pkt_ctrl rtp_size %d", pkt_ctrl->rtp_size);

    struct tsmux_pes_hdr *pes_hdr = &hal->otf_cmd_queue.config.pes_hdr;
    pes_hdr->code = PES_HDR_CODE;
    pes_hdr->stream_id = 0xe0;   // avc
    pes_hdr->pkt_len = 0;  // video is 0
    pes_hdr->marker = PES_HDR_MARKER;
    pes_hdr->scramble = 0;
    pes_hdr->priority = 0;
    pes_hdr->alignment = 1;
    pes_hdr->copyright = 0;
    pes_hdr->original = 0;
    if (hal->otf_cmd_queue.config.hex_ctrl.otf_enable) {
        pes_hdr->flags = 0x81;
        /* add PES extension flags and PES private data size */
        pes_hdr->hdr_len = 5 + pkt_ctrl->pes_stuffing_num + 17;
    } else {
        pes_hdr->flags = 0x80;
        pes_hdr->hdr_len = 5 + pkt_ctrl->pes_stuffing_num;
    }
    /* in case of otf, PTS in PES should be set by tsmux device driver */
    /*
    uint64_t PTS = (cur_out_buf->time_stamp * 9ll) / 100ll;
    pes_hdr->pts39_16 = (0x20 | (((PTS >> 30) & 7) << 1) | 1) << 16;
    pes_hdr->pts39_16 |= ((PTS >> 22) & 0xff) << 8;
    pes_hdr->pts39_16 |= ((((PTS >> 15) & 0x7f) << 1) | 1);
    pes_hdr->pts15_0 = ((PTS >> 7) & 0xff) << 8;
    pes_hdr->pts15_0 |= (((PTS & 0x7f) << 1) | 1);
    ALOGV("tsmux_dq_buf_otf time_stamp %lld, PTS %lld, pts39_16 0x%x, pts15_0 0x%x",
        (long long)cur_out_buf->time_stamp, (long long)PTS, pes_hdr->pts39_16, pes_hdr->pts15_0);
    */
    struct tsmux_ts_hdr *ts_hdr = &hal->otf_cmd_queue.config.ts_hdr;
    ts_hdr->sync = TS_HDR_SYNC;
    ts_hdr->error = 0;
    ts_hdr->priority = 0;
    ts_hdr->pid = 0x1011;
    ts_hdr->scramble = 0;
    ts_hdr->adapt_ctrl = 0;
    /* in case of otf, TS continuity counter should be set by tsmux device driver */

    struct tsmux_rtp_hdr *rtp_hdr = &hal->otf_cmd_queue.config.rtp_hdr;
    rtp_hdr->ver = 0x2;
    rtp_hdr->pad = 0;
    rtp_hdr->ext = 0;
    rtp_hdr->csrc_cnt = 0;
    rtp_hdr->marker = 0;
    rtp_hdr->pl_type = 33;
    rtp_hdr->ssrc = 0xdeadbeef;

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_OTF_SET_CONFIG, &hal->otf_cmd_queue.config);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_OTF_SET_CONFIG");
        return -1;
    }

    ALOGV("tsmux_dq_buf_otf: request dq buf");

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_OTF_DQ_BUF, &hal->otf_cmd_queue);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_OTF_DQ_BUF");
        return -1;
    }

    int64_t curTimeUs = systemTime(SYSTEM_TIME_MONOTONIC) / 1000ll;

    cur_buf_index = hal->otf_cmd_queue.cur_buf_num;
    cur_out_buf = &hal->otf_cmd_queue.out_buf[cur_buf_index];

    int out_buf_size = cur_out_buf->actual_size;
    outbuf = new ABuffer(out_buf_size);
    memcpy(outbuf->data(), hal->otfbuf_addr[cur_buf_index], out_buf_size);

    outbuf->meta()->setInt32("rtp_size", pkt_ctrl->rtp_size);
    outbuf->meta()->setInt64("g2ds", cur_out_buf->g2d_start_stamp);
    outbuf->meta()->setInt64("g2de", cur_out_buf->g2d_end_stamp);
    outbuf->meta()->setInt64("mfcs", cur_out_buf->mfc_start_stamp);
    outbuf->meta()->setInt64("mfce", cur_out_buf->mfc_end_stamp);
    outbuf->meta()->setInt64("tsms", cur_out_buf->tsmux_start_stamp);
    outbuf->meta()->setInt64("tsme", cur_out_buf->tsmux_end_stamp);
    outbuf->meta()->setInt64("kere", cur_out_buf->kernel_end_stamp);
    outbuf->meta()->setInt64("plts", curTimeUs);

    ALOGV("tsmux_dq_buf_otf: dequeu buf, cur_out_buf->actual_size %d", out_buf_size);

    hal->video_frame_count++;

    return 0;
}

void tsmux_q_buf_otf(void *handle)
{
    int ret;
    struct tsmux_hal *hal;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return;
    }

    hal = (struct tsmux_hal *)handle;

    ret = ioctl(hal->tsmux_fd, TSMUX_IOCTL_OTF_Q_BUF, &hal->otf_cmd_queue.cur_buf_num);
    if (ret < 0) {
        ALOGE("fail to ioctl: TSMUX_IOCTL_OTF_Q_BUF");
        return;
    }
}

int tsmux_get_config_otf(void *handle, struct tsmux_config_data *config)
{
    struct tsmux_hal *hal;

    if (!handle) {
        ALOGE("%s: tsmux module was not opened", __FUNCTION__);
        return -ENOENT;
    }

    hal = (struct tsmux_hal *)handle;

    config->pkt_ctrl = &hal->otf_cmd_queue.config.pkt_ctrl;
    config->pes_hdr = &hal->otf_cmd_queue.config.pes_hdr;
    config->ts_hdr = &hal->otf_cmd_queue.config.ts_hdr;
    config->rtp_hdr = &hal->otf_cmd_queue.config.rtp_hdr;

    return 0;
}

}
