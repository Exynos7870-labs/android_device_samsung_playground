#include <sys/types.h>

#ifndef __SBWCDECODER_H__
#define __SBWCDECODER_H__

class SbwcDecoder {
public:
    SbwcDecoder();
    ~SbwcDecoder();
    bool setImage(unsigned int format, unsigned int width,
                  unsigned int height, unsigned int stride);
    bool decode(int inBuf[], size_t inLen[], int outBuf[], size_t outLen[]);
private:
    bool setFmt();
    bool setCrop();
    bool streamOn();
    bool streamOff();
    bool queueBuf(int inBuf[], size_t inLen[], int outBuf[], size_t outLen[]);
    bool dequeueBuf();
    bool reqBufsWithCount(unsigned int count);

    int fd_dev;
    uint32_t mFmtSBWC = 0;
    uint32_t mFmtDecoded = 0;
    unsigned int mNumFd = 0;
    unsigned int mWidth = 0;
    unsigned int mHeight = 0;
    unsigned int mStride = 0;
    uint32_t mLossyBlockSize = 0;
};

#endif
