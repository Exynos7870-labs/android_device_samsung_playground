#ifndef _GIANT_MSCL_IMPL_H_
#define _GIANT_MSCL_IMPL_H_

#include <cinttypes>
#include <vector>
#include <tuple>

#include <system/graphics.h>

#include "uapi.h"
#include "buffer.h"

using TransformCoord = std::tuple<int, int>;

class GiantMsclImpl {
public:
    GiantMsclImpl(bool suppress_error);
    ~GiantMsclImpl();

    bool setSrc(unsigned int srcw, unsigned int srch, unsigned int fmt, unsigned int transform = 0);
    bool setDst(unsigned int dstw, unsigned int dsth, unsigned int fmt);
    bool run(int src_buffer[], int dst_buffer[]);
    bool available() { return !(mFdDev < 0); }

private:
    using Image = std::tuple<unsigned int, unsigned int, unsigned int>;

    // -1: error 0: @count is not sufficient, > 0: okay.
    int generate(mscl_task tasks[], unsigned int count, int src_buffer[], int dst_buffer[]);

    static inline unsigned int width(const Image &img) { return std::get<0>(img); }
    static inline unsigned int height(const Image &img) { return std::get<1>(img); }
    static inline unsigned int format(const Image &img) { return std::get<2>(img); }
    static inline bool rotate90(unsigned int transform) { return (transform & HAL_TRANSFORM_ROT_90) != 0; }
    static inline bool vFlip(unsigned int transform) { return (transform & HAL_TRANSFORM_FLIP_V) != 0; }
    static inline bool hFlip(unsigned int transform) { return (transform & HAL_TRANSFORM_FLIP_H) != 0; }
    static inline unsigned int makeEven(unsigned int val) { return (val + 1) & ~1; }

    int generateTask(const Image &source, const Image &target,
                     unsigned int src_buf_idx, unsigned int dst_buf_idx,
                     mscl_task tasks[], unsigned int count);

    struct Task {
        const static uint32_t FRACTION_BITS = 20;
        const static uint32_t SCALE_FACTOR_1TO1 = 1 << FRACTION_BITS;
        const static uint32_t MSCL_ROTATE_270CCW = 3;
        const static unsigned int HAL_TRANSFORM_FLIP_MASK = 3;
        const static unsigned int MSCL_FLIP_SHIFT = 2;
        const static unsigned int N_HTAPS = 8;
        const static unsigned int N_VTAPS = 4;

        static inline uint32_t CMD_WH(unsigned int width, unsigned int height) {
            return (width << 16) | height;
        }
        static inline uint32_t flipSwap(uint32_t val) { return (val >> 1) | ((val & 1) << 1); }
        static inline unsigned int hStride(const Image &img) { return std::get<0>(img); }
        static inline unsigned int vStride(const Image &img) { return std::get<1>(img); }

        Task(const Image &source, const Image &target, const Buffer &srcbuf, const Buffer &dstbuf, unsigned int transform);

        inline uint32_t scaleFactor(unsigned int from, unsigned int to_base, unsigned int to_perpendicular) {
            unsigned int to = rotate90(mTransform) ? to_perpendicular : to_base;
            return static_cast<uint32_t>((static_cast<uint64_t>(from) << FRACTION_BITS) / to);
        }

        void fill(mscl_task &task_desc);
        bool next();

        const Image &mSource;
        const Image &mTarget;

        unsigned int mTransform;

        const Buffer &mSrcBuffer;
        const Buffer &mDstBuffer;

        unsigned int mSrcWidth;
        unsigned int mSrcHeight;
        unsigned int mDstWidth;
        unsigned int mDstHeight;

        size_t mCurrentPlaneIndex = 0;
        std::vector<TransformCoord> mSrcPlaneCoord;
        std::vector<TransformCoord> mDstPlaneCoord;
    };

    std::vector<Buffer> mBufferStore;
    Image mSrcImage;
    Image mDstImage;
    unsigned int mTransform = 0;
    int mFdDev;
};

#endif //_GIANT_MSCL_IMPL_H_
