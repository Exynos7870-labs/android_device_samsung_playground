#ifndef _EXYNOS_GIANT_MSCL_H_
#define _EXYNOS_GIANT_MSCL_H_

#include <memory>

class GiantMsclImpl;

class GiantMscl {
public:
    GiantMscl (bool suppress_error = false);
    ~GiantMscl ();

    bool setSrc(unsigned int srcw, unsigned int srch, unsigned int fmt, unsigned int transform = 0);
    bool setDst(unsigned int dstw, unsigned int dsth, unsigned int fmt);
    bool run(int src_buffer[], int dst_buffer[]);
    operator bool() { return okay(); }
    bool okay();
private:
    std::unique_ptr<GiantMsclImpl> mImpl;
};

#endif //_EXYNOS_GIANT_MSCL_H_
