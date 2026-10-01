
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <system/graphics.h>
#include <hardware/exynos/ion.h>

#include "log.h"
#include "buffer.h"

#define NR_EXTRA_PIXELS 128

void Buffer::init(int buffer[], unsigned int count, unsigned int fmt, unsigned int width, unsigned int height)
{
    mBuffer[0] = buffer[0];
    mCount++;
    if (fmt != HAL_PIXEL_FORMAT_YCBCR_422_I) { // NV12
        if (count > 1) {
            mBuffer[1] = buffer[1];
        } else {
            mBuffer[1] = mBuffer[0];
            mOffset[1] = width * height;
        }
        mCount++;
    } else { // YUYV
        mHBitPP[0] = 16;
        mVBitPP[0] = 8;
        mHBitPP[1] = 0;
        mVBitPP[1] = 0;
    }
}
// allocates bounce buffers when a job needs to scaling more than once.
// we only allocate a single bounce buffer since we does not use multi-planar format
// between the scaling even thoug the source image or the final destination image are
// multi-planar format.
int Buffer::alloc(unsigned int fmt, unsigned int width, unsigned int height)
{
    int devfd = exynos_ion_open();
    if (devfd < 0)
        return -1;

    // MSCL may read extra 128 pixels after the image region of interest due to its performance.
    // So, we should feed MSCL H/W more memory not to cause buffer overrun.
    size_t len = width * height + NR_EXTRA_PIXELS;
    if (fmt == HAL_PIXEL_FORMAT_YCBCR_422_I) {
        len += len + NR_EXTRA_PIXELS;
    } else {
        len += len / 2;
    }

    int buffd = exynos_ion_alloc(devfd, len, 1, 0);
    if (buffd < 0)
        ALOGERR("failed to allocate for %ux%u (fmt %#x)", width, height ,fmt);

    exynos_ion_close(devfd);

    return buffd;
}

Buffer::~Buffer()
{
    if (mAllocated && (mBuffer[0] >= 0))
        exynos_ion_close(mBuffer[0]);
}

int Buffer::getByteOffset(unsigned int idx, unsigned int x_offset, unsigned int y_offset, unsigned int pixel_stride) const
{
    if (idx < mCount) {
        unsigned int byte_stride = pixel_stride * mHBitPP[idx] / 8;
        return mOffset[idx] + (y_offset * mVBitPP[idx] * byte_stride + x_offset * mHBitPP[idx]) / 8;
    }
    return 0;
}
