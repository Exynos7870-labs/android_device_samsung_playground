#include <cstring>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <log/log.h>

#include <linux/videodev2.h>

#include <hardware/exynos/sbwcdecoder.h>

#define MSCLPATH "/dev/video50"

#define NUM_FD_DECODED	2

#ifndef ALOGERR
#define ALOGERR(fmt, args...) ((void)ALOG(LOG_ERROR, LOG_TAG, fmt " [%s]", ##args, strerror(errno)))
#endif

#define ARRSIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define V4L2_PIX_FMT_NV12M_P010        v4l2_fourcc('P', 'M', '1', '2')
/* 12 Y/CbCr 4:2:0 SBWC */
#define V4L2_PIX_FMT_NV12M_SBWC_8B     v4l2_fourcc('M', '1', 'S', '8')
#define V4L2_PIX_FMT_NV12M_SBWC_10B    v4l2_fourcc('M', '1', 'S', '1')
/* 21 Y/CrCb 4:2:0 SBWC */
#define V4L2_PIX_FMT_NV21M_SBWC_8B     v4l2_fourcc('M', '2', 'S', '8')
#define V4L2_PIX_FMT_NV21M_SBWC_10B    v4l2_fourcc('M', '2', 'S', '1')
/* 12 Y/CbCr 4:2:0 SBWC single */
#define V4L2_PIX_FMT_NV12N_SBWC_8B     v4l2_fourcc('N', '1', 'S', '8')
#define V4L2_PIX_FMT_NV12N_SBWC_10B    v4l2_fourcc('N', '1', 'S', '1')
/* 12 Y/CbCr 4:2:0 SBWC Lossy */
#define V4L2_PIX_FMT_NV12M_SBWCL_8B    v4l2_fourcc('M', '1', 'L', '8')
#define V4L2_PIX_FMT_NV12M_SBWCL_10B   v4l2_fourcc('M', '1', 'L', '1')

enum {
    /* SBWC format */
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC = 0x130,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC  = 0x131,

    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC = 0x132,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC  = 0x133,

    HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_SBWC = 0x134,

    /* SBWC Lossy formats */
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L50 = 0x140,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L75 = 0x141,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L50  = 0x150,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L75  = 0x151,

    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L40 = 0x160,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L60 = 0x161,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L80 = 0x162,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L40  = 0x170,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L60  = 0x171,
    HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L80  = 0x172,
};

SbwcDecoder::SbwcDecoder()
{
    fd_dev = open(MSCLPATH, O_RDWR);
    if (fd_dev < 0) {
        ALOGERR("Failed to open %s", MSCLPATH);
        return;
    }
}

SbwcDecoder::~SbwcDecoder()
{
    if (fd_dev >= 0)
        close(fd_dev);
}

bool SbwcDecoder::reqBufsWithCount(unsigned int count)
{
    v4l2_requestbuffers reqbufs;

    reqbufs.count = count;
    reqbufs.memory = V4L2_MEMORY_DMABUF;

    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    if (ioctl(fd_dev, VIDIOC_REQBUFS, &reqbufs) < 0) {
        ALOGERR("Failed to REQBUFS(SRC)");
        return false;
    }

    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(fd_dev, VIDIOC_REQBUFS, &reqbufs) < 0) {
        ALOGERR("Failed to REQBUFS(DST)");
        return false;
    }

    return true;
}

#define ALIGN_SBWC(val) (((val) + 31) & ~31)

bool SbwcDecoder::setFmt()
{
    v4l2_format fmt;

    memset(&fmt, 0, sizeof(fmt));

    fmt.fmt.pix_mp.height = mHeight;
    // TODO : how to get colorspace, but MSCL driver doesn't use this.
    //fmt.fmt.pix_mp.colorspace = haldataspace_to_v4l2(canvas.getDataspace(), coord.hori, coord.vert);
    fmt.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
    fmt.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
    fmt.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;

    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width = ALIGN_SBWC(mStride);
    fmt.fmt.pix_mp.pixelformat = mFmtSBWC;
    fmt.fmt.pix_mp.flags = mLossyBlockSize;

    if (ioctl(fd_dev, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGERR("Failed to S_FMT(SRC):width(%d), height(%d), fmt(%#x)",
                mStride, mHeight, mFmtSBWC);
        return false;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = mStride;
    fmt.fmt.pix_mp.pixelformat = mFmtDecoded;
    fmt.fmt.pix_mp.flags = 0;

    if (ioctl(fd_dev, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGERR("Failed to S_FMT(DST):width(%d), height(%d), fmt(%#x)",
                mStride, mHeight, mFmtDecoded);
        return false;
    }

    return true;
}

bool SbwcDecoder::setCrop()
{
    v4l2_crop crop;

    crop.c.left = 0;
    crop.c.top = 0;
    crop.c.width = mWidth;
    crop.c.height = mHeight;

    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    if (ioctl(fd_dev, VIDIOC_S_CROP, &crop) < 0) {
        ALOGERR("Failed to S_CROP(SRC):width(%d), height(%d)", mWidth, mHeight);
        return false;
    }

    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(fd_dev, VIDIOC_S_CROP, &crop) < 0) {
        ALOGERR("Failed to S_CROP(DST):width(%d), height(%d)", mWidth, mHeight);
        return false;
    }

    return true;
}

bool SbwcDecoder::streamOn()
{
    enum v4l2_buf_type bufType;

    bufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(fd_dev, VIDIOC_STREAMON, &bufType) < 0) {
        ALOGERR("Failed to STREAMON(SRC)");
        return false;
    }

    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_dev, VIDIOC_STREAMON, &bufType) < 0) {
        ALOGERR("Failed to STREAMON(DST)");
        return false;
    }

    return true;
}

bool SbwcDecoder::streamOff()
{
    enum v4l2_buf_type bufType;

    bufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(fd_dev, VIDIOC_STREAMOFF, &bufType) < 0) {
        ALOGERR("Failed to STREAMOFF(SRC)");
        return false;
    }

    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_dev, VIDIOC_STREAMOFF, &bufType) < 0) {
        ALOGERR("Failed to STREAMOFF(DST)");
        return false;
    }

    return true;
}

//TODO : data_offset is not set, calculate byteused
bool SbwcDecoder::queueBuf(int inBuf[], size_t inLen[],
                           int outBuf[], size_t outLen[])
{
    v4l2_buffer buffer;
    v4l2_plane planes[4];

    memset(&buffer, 0, sizeof(buffer));

    buffer.memory = V4L2_MEMORY_DMABUF;

    memset(planes, 0, sizeof(planes));

    buffer.length = mNumFd;
    buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    for (unsigned int i = 0; i < mNumFd; i++) {
        planes[i].length = inLen[i];
        //planes[i].bytesused = ;
        planes[i].m.fd = inBuf[i];
        //planes[i].data_offset = ;
    }
    buffer.m.planes = planes;

    if (ioctl(fd_dev, VIDIOC_QBUF, &buffer) < 0) {
        ALOGERR("Failed to QBUF(SRC)");
        return false;
    }

    memset(planes, 0, sizeof(planes));

    buffer.length = NUM_FD_DECODED;
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (unsigned int i = 0; i < NUM_FD_DECODED; i++) {
        planes[i].length = outLen[i];
        planes[i].m.fd = outBuf[i];
        //planes[i].data_offset = ;
    }
    buffer.m.planes = planes;

    if (ioctl(fd_dev, VIDIOC_QBUF, &buffer) < 0) {
        ALOGERR("Failed to QBUF(DST)");
        return false;
    }

    return true;
}

bool SbwcDecoder::dequeueBuf()
{
    v4l2_buffer buffer;
    v4l2_plane planes[4];

    memset(&buffer, 0, sizeof(buffer));

    buffer.memory = V4L2_MEMORY_DMABUF;

    buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    memset(planes, 0, sizeof(planes));
    buffer.length = 4;
    buffer.m.planes = planes;

    if (ioctl(fd_dev, VIDIOC_DQBUF, &buffer) < 0) {
        ALOGERR("Failed to DQBUF(SRC)");
        return false;
    }

    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    memset(planes, 0, sizeof(planes));
    buffer.length = 4;
    buffer.m.planes = planes;

    if (ioctl(fd_dev, VIDIOC_DQBUF, &buffer) < 0) {
        ALOGERR("Failed to DQBUF(DST)");
        return false;
    }

    return true;
}

bool SbwcDecoder::decode(int inBuf[], size_t inLen[],
                         int outBuf[], size_t outLen[])
{
    bool ret;

    ret = setFmt();
    if (ret)
        ret = setCrop();
    if (ret)
        ret = reqBufsWithCount(1);
    if (ret)
        ret = streamOn();
    if (ret)
        ret = queueBuf(inBuf, inLen, outBuf, outLen);
    if (ret)
        ret = dequeueBuf();

    streamOff();
    reqBufsWithCount(0);

    return ret;
}

static uint32_t __halfmtSBWC_to_v4l2[][5] = {
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC,     V4L2_PIX_FMT_NV12M_SBWC_8B,   V4L2_PIX_FMT_NV12M,      2, 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC, V4L2_PIX_FMT_NV12M_SBWC_10B,  V4L2_PIX_FMT_NV12M_P010, 2, 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_SBWC,     V4L2_PIX_FMT_NV21M_SBWC_8B,   V4L2_PIX_FMT_NV12M,      2, 0},
    // TODO : NV21M 10bit is not supported
    //{HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_10B_SBWC, V4L2_PIX_FMT_NV21M_SBWC_10B },
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC,      V4L2_PIX_FMT_NV12N_SBWC_8B,   V4L2_PIX_FMT_NV12M,      1, 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC,  V4L2_PIX_FMT_NV12N_SBWC_10B,  V4L2_PIX_FMT_NV12M_P010, 1, 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L50, V4L2_PIX_FMT_NV12M_SBWCL_8B,  V4L2_PIX_FMT_NV12M,      2, 64},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L75, V4L2_PIX_FMT_NV12M_SBWCL_8B,  V4L2_PIX_FMT_NV12M,      2, 96},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L40, V4L2_PIX_FMT_NV12M_SBWCL_10B, V4L2_PIX_FMT_NV12M_P010, 2, 64},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L60, V4L2_PIX_FMT_NV12M_SBWCL_10B, V4L2_PIX_FMT_NV12M_P010, 2, 96},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L80, V4L2_PIX_FMT_NV12M_SBWCL_10B, V4L2_PIX_FMT_NV12M_P010, 2, 128},
};

bool SbwcDecoder::setImage(unsigned int format, unsigned int width,
                           unsigned int height, unsigned int stride)
{
    mWidth = width;
    mHeight = height;
    mStride = stride;

    for (unsigned int i = 0; i < ARRSIZE(__halfmtSBWC_to_v4l2); i++) {
        if (format == __halfmtSBWC_to_v4l2[i][0]) {
            mFmtSBWC = __halfmtSBWC_to_v4l2[i][1];
            mFmtDecoded = __halfmtSBWC_to_v4l2[i][2];
            mNumFd = __halfmtSBWC_to_v4l2[i][3];
            mLossyBlockSize = __halfmtSBWC_to_v4l2[i][4];

            return true;
        }
    }

    ALOGE("Unable to find the proper v4l2 format for HAL format(SBWC) %#x", format);

    return false;
}
