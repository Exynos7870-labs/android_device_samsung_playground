#include <memory>

#include <log/log.h>

#include <hardware/exynos/giant_mscl.h>

#include "giant_mscl_impl.h"

GiantMscl::GiantMscl(bool suppress_error)
{
#ifdef USE_GIANT_MSCL
#if __cplusplus < 201402L
    // C++11
    mImpl.reset(new GiantMsclImpl(suppress_error));
#else
    // C++14
    mImpl = std::make_unique<GiantMsclImpl>(suppress_error);
#endif
#else // !USE_GIANT_MSCL
    if (!suppress_error)
        ALOGE("GiantMSCL is not availble in this product");
#endif // USE_GIANT_MSCL
}

GiantMscl::~GiantMscl()
{
}

bool GiantMscl::setSrc(unsigned int srcw, unsigned int srch, unsigned int fmt, unsigned int transform)
{
    return mImpl && mImpl->setSrc(srcw, srch, fmt, transform);
}

bool GiantMscl::setDst(unsigned int dstw, unsigned int dsth, unsigned int fmt)
{
    return mImpl && mImpl->setDst(dstw, dsth, fmt);
}

bool GiantMscl::run(int src_buffer[], int dst_buffer[])
{
    return mImpl && mImpl->run(src_buffer, dst_buffer);
}

bool GiantMscl::okay()
{
    return mImpl && mImpl->available();
}
