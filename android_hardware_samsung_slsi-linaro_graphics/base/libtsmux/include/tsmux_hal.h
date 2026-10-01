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
#ifndef TSMUX_HAL_H
#define TSMUX_HAL_H

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/ADebug.h>

#include "tsmux.h"

#define TS_PKT_COUNT_PER_RTP    7
#define RTP_HEADER_SIZE     	12
#define TS_PACKET_SIZE      	188
#define TS_HEADER_SIZE      	4

#define PES_HDR_CODE            0x1
#define PES_HDR_MARKER          0x2

#define TS_HDR_SYNC             0x47

namespace android {

struct tsmux_config_data {
    struct tsmux_pkt_ctrl *pkt_ctrl;
    struct tsmux_pes_hdr *pes_hdr;
    struct tsmux_ts_hdr *ts_hdr;
    struct tsmux_rtp_hdr *rtp_hdr;
};

void *tsmux_open(bool enable_hdcp, bool use_hevc, bool use_lpcm);
void tsmux_close(void *handle);

void tsmux_get_switching_info(void *handle, int *rtpSeqNum,
    int *patCC, int *pmtCC, int *videoCC, int *audioCC);
void tsmux_set_switching_info(void *handle, int rtpSeqNum,
    int patCC, int pmtCC, int videoCC, int audioCC);

int tsmux_init_m2m(void *handle);
void tsmux_deinit_m2m(void *handle);
int tsmux_packetize_m2m(void *handle, sp<ABuffer> *inbufs,
        sp<ABuffer> *outbufs);
int tsmux_init_otf(void *handle, uint32_t width, uint32_t height);
void tsmux_deinit_otf(void *handle);
int tsmux_dq_buf_otf(void *handle, sp<ABuffer> &outbuf);
void tsmux_q_buf_otf(void *handle);
int tsmux_get_config_otf(void *handle, struct tsmux_config_data *config);
}

#endif
