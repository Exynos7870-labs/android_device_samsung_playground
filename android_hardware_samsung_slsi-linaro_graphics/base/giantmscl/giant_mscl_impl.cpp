#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <log/log.h>

#include <exynos_format.h> // hardware/smasung_slsi/exynos/include

#include "log.h"
#include "giant_mscl_impl.h"

#include "debug.h"

#define MSCL_FMT_NV12 0
#define MSCL_FMT_NV21 16
#define MSCL_FMT_YUYV 10

const static unsigned int NR_PIXELS_8K = 8192;
const static unsigned int NR_PIXELS_16K = 16384;

const static char *giant_mscl_dev = "/dev/scaler_ext";

unsigned int hal_to_dev_format_table[][2] = {
    {HAL_PIXEL_FORMAT_YCBCR_422_I, MSCL_FMT_YUYV},
    {HAL_PIXEL_FORMAT_YCRCB_420_SP, MSCL_FMT_NV21},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M, MSCL_FMT_NV12},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M, MSCL_FMT_NV21}
};

unsigned int getDeviceFormat(unsigned int halfmt)
{
    for (auto &ent: hal_to_dev_format_table)
        if (halfmt == ent[0])
            return ent[1];
    return ~0;
}

struct ImageSizeError {
    ImageSizeError(unsigned int width, unsigned int height, unsigned int format, const char *typemsg)
        : mWidth(width), mHeight(height), mFormat(format), mMsg(typemsg)
    {}
    ~ImageSizeError() {
        if (mEnabled)
            ALOGE("invalid image size %ux%u of format %d for %s", mWidth, mHeight, mFormat, mMsg);
    }

    void discard() { mEnabled = false; }

    bool mEnabled = true;
    unsigned int mWidth;
    unsigned int mHeight;
    unsigned int mFormat;
    const char *mMsg;
};

static bool checkImageType(unsigned int width, unsigned int height,
                           unsigned int minw, unsigned int minh,
                           unsigned int format, const char * typemsg)
{
    ImageSizeError error(width, height, format, typemsg);

    if (width & 1)
        return false;

    if ((format != HAL_PIXEL_FORMAT_YCBCR_422_I) && (height & 1))
        return false;
    if ((width > NR_PIXELS_16K) || (height > NR_PIXELS_16K))
        return false;
    if ((width < minw) || (height < minh))
        return false;
    // If horizontal or vertical number of pixels are larger than 8K,
    // image is partitioned into two smaller images with the same size
    // which is half of the image size(width or height).
    // But the smaller images should also satisfy the restriction of the image format.
    if ((width > NR_PIXELS_8K) && ((width % 4) != 0))
        return false;
    if (height > NR_PIXELS_8K) {
        if (format != HAL_PIXEL_FORMAT_YCBCR_422_I) {
             if ((height % 4) != 0)
                return false;
        } else {
             if ((height % 2) != 0)
                return false;
        }
    }

    error.discard();

    for (auto &ent: hal_to_dev_format_table)
        if (format == ent[0])
            return true;

    ALOGE("pixel format %d is not supported for %s", format, typemsg);

    return false;
}

GiantMsclImpl::GiantMsclImpl(bool suppress_error)
{
    mSrcImage = std::make_tuple(16, 16, HAL_PIXEL_FORMAT_YCRCB_420_SP);
    mDstImage = std::make_tuple(4, 4, HAL_PIXEL_FORMAT_YCRCB_420_SP);

    mFdDev = ::open(giant_mscl_dev, O_WRONLY);
    if (!suppress_error && (mFdDev < 0))
        ALOGERR("failed to open %s", giant_mscl_dev);
}

GiantMsclImpl::~GiantMsclImpl()
{
    if (mFdDev >= 0)
        ::close(mFdDev);
}

bool GiantMsclImpl::setSrc(unsigned int srcw, unsigned int srch, unsigned int fmt, unsigned int transform) {
    if (!checkImageType(srcw, srch, 16, 16, fmt, "source"))
        return false;

    if (transform & ~HAL_TRANSFORM_ROT_270) {
        ALOGE("invalid transform %d", transform);
        return false;
    }

    mSrcImage = std::make_tuple(srcw, srch, fmt);
    mTransform = transform;
    return true;
}

bool GiantMsclImpl::setDst(unsigned int dstw, unsigned int dsth, unsigned int fmt) {
    if (!checkImageType(dstw, dsth, 4, 4, fmt, "destination"))
        return false;

    mDstImage = std::make_tuple(dstw, dsth, fmt);
    return true;
}

bool GiantMsclImpl::run(int src_buffer[], int dst_buffer[])
{
    mscl_job job;
    mscl_task task[6];
    job.tasks = task;

    mBufferStore.clear();

    job.taskcount = generate(task, 6, src_buffer, dst_buffer);
    if (job.taskcount < 1) {
        return false;
    }

    job.version = 0;

    showJob(&job);
    if (::ioctl(mFdDev, MSCL_IOC_JOB, &job) < 0) {
        ALOGERR("failed to run Giant MSCL");
        return false;
    }

    return true;
}

static inline unsigned int round_up(unsigned int val, unsigned int factor)
{
    // factor & (factor - 1) == 0.
    //return ((val + factor - 1) / factor) * factor;
    return (val + factor - 1) & ~(factor - 1);
}
static inline bool is_aligned(unsigned int val, unsigned int factor)
{
    // factor & (factor - 1) == 0.
    return !(val & (factor - 1));
}

int GiantMsclImpl::generate(mscl_task tasks[], unsigned int count, int src_buffer[], int dst_buffer[])
{
    unsigned int task_count = 0;
    unsigned int srcBufferIdx = 0;
    Image target = mSrcImage;

    mBufferStore.emplace_back(src_buffer, format(mSrcImage), width(mSrcImage), height(mSrcImage));
    // last element of mBufferStore is always the destination buffer.
    mBufferStore.emplace_back(dst_buffer, format(mDstImage), width(mDstImage), height(mDstImage));

    do {
        Image source = target;

        unsigned int srcWidth = width(source);
        unsigned int srcHeight = height(source);

        if (rotate90(mTransform))
            std::swap(srcWidth, srcHeight);

        unsigned int targetWidth, targetHeight;

        if (srcWidth > width(mDstImage)) {
            targetWidth = std::max(makeEven(srcWidth / 4), width(mDstImage));
            if (srcWidth > NR_PIXELS_8K)
                targetWidth = round_up(targetWidth, 4);
        } else {
            targetWidth = std::min(srcWidth * 8, width(mDstImage));
            // if targetWidth is larger than 8K, the restriction by partitioned processing is checked by setDst().
            // if srcWidth is not proper for partitioned procesing, insert an upscaling without partitioning.
            if (targetWidth > NR_PIXELS_8K) {
                unsigned int factor = 4;
                if (rotate90(mTransform) && (format(source) == HAL_PIXEL_FORMAT_YCBCR_422_I))
                    factor = 2;
                if (!is_aligned(srcWidth, factor))
                    targetWidth = NR_PIXELS_8K;
            }
        }

        if (srcHeight > height(mDstImage)) {
            targetHeight = std::max(makeEven(srcHeight / 4), height(mDstImage));
            if (srcHeight > NR_PIXELS_8K) {
                if (format(mDstImage) == HAL_PIXEL_FORMAT_YCBCR_422_I)
                    round_up(targetHeight, 2);
                else
                    round_up(targetHeight, 4);
            }
        } else {
            targetHeight = std::min(srcHeight * 8, height(mDstImage));
            if (targetHeight > NR_PIXELS_8K) {
                unsigned int factor = 4;
                if (!rotate90(mTransform) && (format(source) == HAL_PIXEL_FORMAT_YCBCR_422_I))
                    factor = 2;
                if (!is_aligned(srcHeight, factor))
                    targetHeight = NR_PIXELS_8K;
            }
        }

        target = std::make_tuple(targetWidth, targetHeight, format(mDstImage));

        unsigned int targetBufferIdx = mBufferStore.size() - 1;
        if (target != mDstImage) {
            targetBufferIdx = srcBufferIdx + 1;
            auto iter = mBufferStore.emplace(mBufferStore.begin() + targetBufferIdx,
                                             format(target), width(target), height(target));
            if (iter->get(0) < 0)
                return -1;
        }

        int ret = generateTask(source, target, srcBufferIdx, targetBufferIdx, &tasks[task_count], count - task_count);
        if (ret < 1)
            return ret;

        task_count += ret;
        srcBufferIdx++;
    } while (target != mDstImage);

    return static_cast<int>(task_count);
}

int GiantMsclImpl::generateTask(const Image &source, const Image &target,
                            unsigned int src_buf_idx, unsigned int dst_buf_idx,
                            mscl_task tasks[], unsigned int count) {
    unsigned int task_count = 0;

    // TODO: We rotate/flip image at the first stage for the simple design.
    //       It is efficient unless the destination size is smaller than source/4.
    //       To tackle this unefficiency in extreme downscaling with rotation/flip,
    //       we should introduce an improved design to defer roation/flip until the
    //       last stage of downscaling.
    Task task(source, target, mBufferStore[src_buf_idx], mBufferStore[dst_buf_idx], mTransform);

    do {
        if (task_count >= count)
            return 0;

        task.fill(tasks[task_count++]);
    } while (task.next());

    mTransform = 0;

    return task_count;
}

static inline TransformCoord denormalize(int x, int y)
{
    return std::make_tuple((x + 1) / 2, (1 - y) / 2);
}

struct TransformMatrix {
    struct SquareMatrix {
        SquareMatrix(int x0, int y0, int x1, int y1) : mMat{x0, y0, x1, y1} { }
        void makeIdentity() {
            for (int i = 0; i < 4; i++)
                mMat[i] = mTransformMatrix[0].mMat[i];
        }
        void invertY() {
            if (mMat[1] > 0) mMat[1] = -mMat[1];
            if (mMat[3] < 0) mMat[3] = -mMat[3];
        }
        void invertX() {
            if (mMat[0] < 0) mMat[0] = -mMat[0];
            if (mMat[2] > 0) mMat[2] = -mMat[2];
        }
        int mMat[4];
        TransformCoord multiplyAndDenormalize(TransformCoord &coord) {
            auto x = mMat[0] * std::get<0>(coord) + mMat[1] * std::get<1>(coord);
            auto y = mMat[2] * std::get<0>(coord) + mMat[3] * std::get<1>(coord);
            return denormalize(x, y);
        }
    };

    const static SquareMatrix mTransformMatrix[8];

    // NOTE: do not return a reference because returned matrix might be modified.
    static SquareMatrix get(unsigned int transform) { return mTransformMatrix[transform]; }
};

const TransformMatrix::SquareMatrix TransformMatrix::mTransformMatrix[8] = {
    {  1,  0,  0,  1}, // identity matrix
    { -1,  0,  0,  1}, // HAL_TRANSFORM_FLIP_H
    {  1,  0,  0, -1}, // HAL_TRANSFORM_FLIP_V
    { -1,  0,  0, -1}, // HAL_TRANSFORM_ROT_180
    {  0,  1, -1,  0}, // HAL_TRANSFORM_ROT_90
    {  0,  1,  1,  0}, // HAL_TRANSFORM_ROT_90 | HAL_TRANSFORM_FLIP_H
    {  0, -1, -1,  0}, // HAL_TRANSFORM_ROT_90 | HAL_TRANSFORM_FLIP_V
    {  0, -1,  1,  0}, // HAL_TRANSFORM_ROT_270
};

GiantMsclImpl::Task::Task(const Image &source, const Image &target, const Buffer &srcbuf, const Buffer &dstbuf, unsigned int transform)
    : mSource(source), mTarget(target), mTransform(transform), mSrcBuffer(srcbuf), mDstBuffer(dstbuf) {
    mSrcWidth = hStride(mSource);
    mSrcHeight = vStride(mSource);
    mDstWidth = hStride(mTarget);
    mDstHeight = vStride(mTarget);

    // temporary swap for convenient calculation
    if (mTransform & HAL_TRANSFORM_ROT_90)
        std::swap(mDstWidth, mDstHeight);

    unsigned int horizontal_div = 1;
    unsigned int vertical_div = 1;

    if (mSrcWidth > NR_PIXELS_8K || mDstWidth > NR_PIXELS_8K)
        horizontal_div = 2;
    if (mSrcHeight > NR_PIXELS_8K || mDstHeight > NR_PIXELS_8K)
        vertical_div = 2;

    mSrcWidth /= horizontal_div;
    mDstWidth /= horizontal_div;
    mSrcHeight /= vertical_div;
    mDstHeight /= vertical_div;

    if (mTransform & HAL_TRANSFORM_ROT_90)
        std::swap(mDstHeight, mDstWidth);

    auto transformMatrix = TransformMatrix::get(transform);

    mSrcPlaneCoord.emplace_back(-1, 1);

    if (horizontal_div == 2) {
        mSrcPlaneCoord.emplace_back(1, 1);
        if (vertical_div == 2) {
            mSrcPlaneCoord.emplace_back(-1, -1);
            mSrcPlaneCoord.emplace_back(1, -1);
        } else {
            transformMatrix.invertY();
        }
    } else if (vertical_div == 2) {
        mSrcPlaneCoord.emplace_back(-1, -1);
        transformMatrix.invertX();
    } else {
        transformMatrix.makeIdentity();
    }

    for (auto &coord: mSrcPlaneCoord) {
        mDstPlaneCoord.emplace_back(transformMatrix.multiplyAndDenormalize(coord));
        coord = std::apply(denormalize, coord);
    }
}

bool GiantMsclImpl::Task::next() {
    return mSrcPlaneCoord.size() > ++mCurrentPlaneIndex;
}

void GiantMsclImpl::Task::fill(mscl_task &task_desc) {
    task_desc = { };
    uint32_t hScaleFactor = scaleFactor(hStride(mSource), hStride(mTarget), vStride(mTarget));
    uint32_t vScaleFactor = scaleFactor(vStride(mSource), vStride(mTarget), hStride(mTarget));
    auto [srcHOffset, srcVOffset] = mSrcPlaneCoord[mCurrentPlaneIndex];
    auto [dstHOffset, dstVOffset] = mDstPlaneCoord[mCurrentPlaneIndex];

    srcHOffset *= mSrcWidth;
    srcVOffset *= mSrcHeight;
    dstHOffset *= mDstWidth;
    dstVOffset *= mDstHeight;

    bool needHorizontalInterpolation = (hScaleFactor != SCALE_FACTOR_1TO1);
    bool needVerticalInterpolation = (vScaleFactor != SCALE_FACTOR_1TO1);

    // interpolation starts after (src[HV]Offset + N_TABS / 2)
    // because we configure horizontal initial phase with N_TABS / 2.
    // Therefore we should shift the offset of address by N_TABS / 2 lower.
    unsigned int horizontalDisplacement = 0;
    if (needHorizontalInterpolation && (srcHOffset > 0))
        horizontalDisplacement = N_HTAPS / 2;

    unsigned int verticalDisplacement = 0;
    if (needVerticalInterpolation && (srcVOffset > 0))
        verticalDisplacement = N_VTAPS / 2;

    task_desc.buf[MSCL_SRC].count = mSrcBuffer.count();
    for (unsigned int i = 0; i < mSrcBuffer.count(); i++) {
        task_desc.buf[MSCL_SRC].dmabuf[i] = mSrcBuffer.get(i);
        task_desc.buf[MSCL_SRC].offset[i] = mSrcBuffer.getByteOffset(
            i, srcHOffset - horizontalDisplacement, srcVOffset - verticalDisplacement, hStride(mSource));
        // We only support YCbCr422 interleaved (YUYV) and YCbCr420 semi-planar(nv12/nv21)
        // Chage to displacement is only applicable to planar formats
        // because it is aplied to buffer offset calculations.
        // But horizontalDisplacement is not divided for semi-planar format
        // because the stride of chroma is the same as luma.
        // See the fact below that vertical initial phase of chroma of nv12/nv21 is also divided by 2.
        verticalDisplacement /= 2;
    }

    if (needHorizontalInterpolation && (srcHOffset > 0)) {	// NOTE: if the source plane is on the right
        task_desc.cmd[MSCL_SRC_YH_IPHASE] = (N_HTAPS / 2) << FRACTION_BITS;
        task_desc.cmd[MSCL_SRC_CH_IPHASE] = task_desc.cmd[MSCL_SRC_YH_IPHASE];
        // luma and chroma are interleaved in YUYV
        // while initial phase of luman and chroma should be handled separately.
        // So, initial phase of chroma in YUYV should be half of that of luma
        // because YUYV is YCbCr422.
        // This rule is not applied to YCbCr420 semi-planar(NV12 and NV21)
        // because luma and chroma are stored separately and the stride of luma and chroma is the same.
        if (getDeviceFormat(format(mSource)) == MSCL_FMT_YUYV)
            task_desc.cmd[MSCL_SRC_CH_IPHASE] /= 2;
    }

    if (needVerticalInterpolation && (srcVOffset > 0)) {
        task_desc.cmd[MSCL_SRC_YV_IPHASE] = (N_VTAPS / 2) << FRACTION_BITS;
        task_desc.cmd[MSCL_SRC_CV_IPHASE] = task_desc.cmd[MSCL_SRC_YV_IPHASE];
        // Chroma of nv12/nv21 is horizontally subsampled.
        if ((getDeviceFormat(format(mSource)) == MSCL_FMT_NV12) || (getDeviceFormat(format(mSource)) == MSCL_FMT_NV12))
            task_desc.cmd[MSCL_SRC_CV_IPHASE] /= 2;
    }

    task_desc.buf[MSCL_DST].count = mDstBuffer.count();
    for (unsigned int i = 0; i < mDstBuffer.count(); i++) {
        task_desc.buf[MSCL_DST].dmabuf[i] = mDstBuffer.get(i);
        task_desc.buf[MSCL_DST].offset[i] = mDstBuffer.getByteOffset(i, dstHOffset, dstVOffset, hStride(mTarget));
    }

    task_desc.cmd[MSCL_SRC_CFG] = getDeviceFormat(format(mSource));
    uint32_t horizontalInterpolationMargin = 0;
    uint32_t verticalInterpolationMargin = 0;
    if (needHorizontalInterpolation && (mSrcWidth < hStride(mSource))) {
        horizontalInterpolationMargin += N_HTAPS / 2;
        if (!srcHOffset)		// NOTE: if the source plane is on the left
            horizontalInterpolationMargin += N_HTAPS / 2;
    }
    if (needVerticalInterpolation && (mSrcHeight < vStride(mSource))) {
        verticalInterpolationMargin += N_VTAPS / 2;
        if (!srcVOffset)
            verticalInterpolationMargin += N_VTAPS / 2;
    }
    task_desc.cmd[MSCL_SRC_WH] = CMD_WH(mSrcWidth + horizontalInterpolationMargin, mSrcHeight + verticalInterpolationMargin);

    task_desc.cmd[MSCL_SRC_SPAN] = hStride(mSource);
    if (getDeviceFormat(format(mSource)) != MSCL_FMT_YUYV)
        task_desc.cmd[MSCL_SRC_SPAN] |= hStride(mSource) << 16; // chroma stride
    task_desc.cmd[MSCL_DST_CFG] = getDeviceFormat(format(mTarget));
    task_desc.cmd[MSCL_DST_WH] = CMD_WH(mDstWidth, mDstHeight);
    task_desc.cmd[MSCL_DST_SPAN] = hStride(mTarget);
    if (getDeviceFormat(format(mTarget)) != MSCL_FMT_YUYV)
        task_desc.cmd[MSCL_DST_SPAN] |= hStride(mTarget) << 16;

    if (rotate90(mTransform)) {
        task_desc.cmd[MSCL_H_RATIO] = vScaleFactor;
        task_desc.cmd[MSCL_V_RATIO] = hScaleFactor;
    } else {
        task_desc.cmd[MSCL_H_RATIO] = hScaleFactor;
        task_desc.cmd[MSCL_V_RATIO] = vScaleFactor;
    }
    // NOTE: The direction of rotation described in MSCL UM is CCW while Android rotation diretion is CW.
    unsigned int flipflag = mTransform & ~HAL_TRANSFORM_ROT_90;
    task_desc.cmd[MSCL_ROT_CFG] = flipflag << MSCL_FLIP_SHIFT;
    if (rotate90(mTransform))
        task_desc.cmd[MSCL_ROT_CFG] |= MSCL_ROTATE_270CCW;
}
