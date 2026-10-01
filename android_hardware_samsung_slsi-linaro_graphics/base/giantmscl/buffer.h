#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <exynos_format.h> // hardware/smasung_slsi/exynos/include

class Buffer {
public:
    Buffer(int buffer[], unsigned int fmt, unsigned int width, unsigned int height): mAllocated(false) {
        unsigned int count = 1;
        if ((fmt == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M) || (fmt == HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M))
            count = 2;

        init(buffer, count, fmt, width, height);
    }

    Buffer(unsigned int fmt, unsigned int width, unsigned int height): mAllocated(true) {
        int fd = alloc(fmt, width, height);
        if (fd >= 0)
            init(&fd, 1, fmt, width, height);
    }

    Buffer(Buffer &&buf) noexcept {
        for (unsigned int i = 0; i < 2; i++) {
            mBuffer[i] = buf.mBuffer[i];
            mOffset[i] = buf.mOffset[i];
            mHBitPP[i] = buf.mHBitPP[i];
            mVBitPP[i] = buf.mVBitPP[i];
        }

        mCount = buf.mCount;
        mAllocated = buf.mAllocated;

        buf.mAllocated = false;
        buf.mCount = 0;
    }

    Buffer &operator=(Buffer &&buf) {
        for (unsigned int i = 0; i < 2; i++) {
            mBuffer[i] = buf.mBuffer[i];
            mOffset[i] = buf.mOffset[i];
            mHBitPP[i] = buf.mHBitPP[i];
            mVBitPP[i] = buf.mVBitPP[i];
        }

        mCount = buf.mCount;
        mAllocated = buf.mAllocated;

        buf.mAllocated = false;
        buf.mCount = 0;

        return *this;
    }

    ~Buffer();

    void init(int buffer[], unsigned int count, unsigned int fmt, unsigned int width, unsigned int height);
    int alloc(unsigned int fmt, unsigned int width, unsigned int height);

    int get(unsigned int idx) const { return (idx > 1) ? -1 : mBuffer[idx]; }
    int getByteOffset(unsigned int idx, unsigned int offset) const { return (idx > 1) ? 0 : mOffset[idx] + offset; }
    int getByteOffset(unsigned int idx, unsigned int x_offset, unsigned int y_offset, unsigned int pixel_stride) const;
    int operator[](unsigned int idx) { return get(idx); }
    unsigned int count() const { return mCount; }
private:
    int mBuffer[2] = {-1, -1};
    int mOffset[2] = {0, 0};
    unsigned char mHBitPP[2] = {8, 8}; // NV12
    unsigned char mVBitPP[2] = {8, 4}; // NV12
    unsigned int mCount = 0;
    bool mAllocated;
};

#endif //_BUFFER_H_
