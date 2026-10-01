#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <regex>
#include <fstream>
#include <tuple>

#include <sys/mman.h>
#include <unistd.h>

#include <log/log.h>
#include <system/graphics.h>

#include <json/json.h>

#include <exynos_format.h> // hardware/smasung_slsi/exynos/include

#include <hardware/exynos/giant_mscl.h>
#include <hardware/exynos/ion.h>

/* the foramt of JSON command:
 * {
 *     "repeat": <number>,
 *     "jobs": [
 *         {
 *             "src": [ <width>, <height>, "<nv12|nv12m|yuyv>", "<path-to-image-file>"],
 *             "dst": [ <width>, <height>, "<nv12|nv12m|yuyv>", "<path-to-image-file>"],
 *             "rotation": <0|90|180|270>,
 *             "flip": <"x"|"y">,
 *             "expected_success": <true|false>,
 *         },
 *         ...
 *     ]
 * }
 */

// WxH/NV12 WxH/YUYV
struct Usage {
    Usage(const char *prg): mProg(prg) { }
    void operator()() {
        std::cout << mProg << " -j <jobs_description.json>" << std::endl;
        std::cout << mProg << " -i [width]x[height]/[fmt]@[file] -o [width]x[height]/[fmt]@[file] [-t h|v|90|270|180|h90|h270]"  << std::endl;
        std::cout << " formats: nv12, nv12m, yuyv" << std::endl;
    }
    std::string mProg;
};

unsigned int getFormat(const std::string &fmtstr)
{
    if (fmtstr == "nv12")
        return HAL_PIXEL_FORMAT_YCRCB_420_SP;
    if (fmtstr == "nv12m")
        return HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M;
    if (fmtstr == "yuyv")
        return HAL_PIXEL_FORMAT_YCBCR_422_I;
    if (fmtstr == "nv16")
        return HAL_PIXEL_FORMAT_YCBCR_422_SP;
    if (fmtstr == "yv12")
        return HAL_PIXEL_FORMAT_YV12;
    if (fmtstr == "rgba8888")
        return HAL_PIXEL_FORMAT_RGBA_8888;

    return 0;
}

static const char *formatName(unsigned int fmt)
{
    switch (fmt) {
        case HAL_PIXEL_FORMAT_YCRCB_420_SP:
            return "nv12";
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
            return "nv12m";
        case HAL_PIXEL_FORMAT_YCBCR_422_I:
            return "yuyv";
        case HAL_PIXEL_FORMAT_YCBCR_422_SP:
            return "nv16";
        case HAL_PIXEL_FORMAT_YV12:
            return "yv12";
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return "rgba8888";
        }

    return "unknonwn";
}

static std::string transformName(unsigned int transform)
{
    std::string name;

    if (transform == HAL_TRANSFORM_ROT_270) {
        name = "270";
        transform &= ~HAL_TRANSFORM_ROT_270;
    }

    if (transform == HAL_TRANSFORM_ROT_180) {
        name = "180";
        transform &= ~HAL_TRANSFORM_ROT_180;
    }

    if (transform & HAL_TRANSFORM_FLIP_H)
        name += "hflip";

    if (transform & HAL_TRANSFORM_FLIP_V)
        name += "vflip";

    if (transform & HAL_TRANSFORM_ROT_90)
        name += "90";

    return name;
}

static bool readFile(int fd, std::ifstream &ifs, size_t len)
{
    char *addr;

    addr = reinterpret_cast<char *>(mmap(0, len, PROT_WRITE, MAP_SHARED, fd, 0));
    if (addr == MAP_FAILED) {
        std::cerr << "failed to mmap buffer for read" << std::endl;
        return false;
    }

    bool result = !!ifs.read(addr, len);
    munmap(addr, len);
    return result;
}

static bool storeFile(int fd, std::ofstream &ofs, size_t len)
{
    char *addr;

    addr = reinterpret_cast<char *>(mmap(0, len, PROT_READ, MAP_SHARED, fd, 0));
    if (addr == MAP_FAILED) {
        std::cerr << "failed to mmap buffer for write" << std::endl;
        return false;
    }

    bool result = !!ofs.write(addr, len);
    munmap(addr, len);
    return result;
}

using ImageQuad = std::tuple<unsigned int, unsigned int, unsigned int, std::string>;
static unsigned int width(ImageQuad img) { return std::get<0>(img); }
static unsigned int height(ImageQuad img) { return std::get<1>(img); }
static unsigned int format(ImageQuad img) { return std::get<2>(img); }
static std::string &path(ImageQuad img) { return std::get<3>(img); }

#define NR_EXTRA_PIXELS 128
void getBufferSize(ImageQuad &img, unsigned int cnt[2], bool extra)
{
    cnt[0] = width(img) * height(img);
    cnt[1] = 0;

    if (format(img) == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M) {
        cnt[1] = cnt[0] / 2;
        if (extra)
            cnt[1] += NR_EXTRA_PIXELS;
    } else if (format(img) == HAL_PIXEL_FORMAT_YCRCB_420_SP) {
        cnt[0] += cnt[0] / 2;
    } else { // yuyv
        cnt[0] *= 2;
        if (extra)
            cnt[0] += NR_EXTRA_PIXELS; // YUYV needs x2 extra bytes becuase its bpp of plane 0 is 2.
    }
    if (extra)
        cnt[0] += NR_EXTRA_PIXELS;
}

class AutoFd {
public:
    AutoFd(int fd = -1) : mFd(fd){ }
    ~AutoFd() { if (mFd >= 0) ::close(mFd); }
    int operator=(int fd) {
        if (mFd >= 0) ::close(mFd);
        mFd = fd;
        return mFd;
    }
    operator int() { return mFd; }
private:
    int mFd = -1;
};

class AutoFd2 {
public:
    AutoFd2() { }
    ~AutoFd2() { }
    void set(int idx, int fd) { mFd[idx] = fd; }
    void get(int fds[2]) { fds[0] = mFd[0]; fds[1] = mFd[1]; }
    AutoFd &operator[](int idx) { return mFd[idx]; }
private:
    AutoFd mFd[2];
};

struct Reporter {
    ImageQuad &mSrc;
    ImageQuad &mDst;
    unsigned int mTransform;
    bool mExpectedResult;
    bool mOkay = false;
    Reporter(ImageQuad &src, ImageQuad &dst, unsigned int transform, bool expected_result)
        : mSrc(src), mDst(dst), mTransform(transform), mExpectedResult(expected_result) {
    }

    ~Reporter() {
        if (mExpectedResult == mOkay)
            std::cout << "  [OKAY]   expected";
        else
            std::cout << "  [FAIL] unexpected";

        if (mOkay)
            std::cout << " SUCCESS";
        else
            std::cout << " FAILURE";

        std::cout << " [" << width(mSrc) << "x" << height(mSrc) << ", " << formatName(format(mSrc)) << " @ " << path(mSrc) << "]";
        std::cout << " --[" << transformName(mTransform) << "]-->";
        std::cout << " [" << width(mDst) << "x" << height(mDst) << ", " << formatName(format(mDst)) << " @ " << path(mDst) << "]" << std::endl;
    }

    bool fail() { mOkay = false; return mOkay == mExpectedResult; }
    bool okay() { mOkay = true; return mOkay == mExpectedResult; }
};

bool runSingle(GiantMscl &mscl, ImageQuad &src, ImageQuad &dst, unsigned int transform, bool expected_result = true)
{
    std::ifstream ifs;
    std::ofstream ofs;
    Reporter reporter(src, dst, transform, expected_result);

    if (path(src).size() > 0) {
        ifs.open(path(src), std::ios::binary);
        if (!ifs) {
            std::cerr << "Failed to open " << path(src) << std::endl;
            return reporter.fail();
        }
    }

    if (path(dst).size() > 0) {
        ofs.open(path(dst), std::ios::binary | std::ios::trunc | std::ios::out);
        if (!ofs) {
            std::cerr << "failed to create " << path(dst) << std::endl;
            return reporter.fail();
        }
    }

    AutoFd ion(exynos_ion_open());
    if (ion < 0)
        return reporter.fail();

    unsigned int src_buf_len[2];
    unsigned int src_imglen[2];
    unsigned int dst_buf_len[2];
    unsigned int dst_imglen[2];
    AutoFd2 srcbuf, dstbuf;

    getBufferSize(src, src_buf_len, true);
    getBufferSize(src, src_imglen, false);
    getBufferSize(dst, dst_buf_len, true);
    getBufferSize(dst, dst_imglen, false);

    for (unsigned int i = 0; i < 2; i++) {
        if (dst_buf_len[i]) {
            srcbuf[i] = exynos_ion_alloc(ion, src_buf_len[i], 1, 0);
            if (srcbuf[i] < 0) {
                std::cerr << "failed to allocate " << src_buf_len[i] << " bytes from ION" << std::endl;
                return reporter.fail();
            }
            if (path(src).size() > 0) {
                if (!readFile(srcbuf[i], ifs, src_imglen[i])) {
                    std::cerr << "failed to read " << src_imglen[i] << " bytes from '" << path(src) << "'" << std::endl;
                    return reporter.fail();
                }
            }
        }
    }

    for (unsigned int i = 0; i < 2; i++) {
        if (dst_buf_len[i]) {
            dstbuf[i] = exynos_ion_alloc(ion, dst_buf_len[i], 1, 0);
            if (dstbuf[i] < 0) {
                std::cerr << "failed to allocate " << dst_buf_len[i] << " bytes from ION" << std::endl;
                return reporter.fail();
            }
        }
    }

    int srcfds[2], dstfds[2];

    srcbuf.get(srcfds);
    dstbuf.get(dstfds);

    if (!mscl.setSrc(width(src), height(src), format(src), transform))
        return reporter.fail();
    if (!mscl.setDst(width(dst), height(dst), format(dst)))
        return reporter.fail();
    if (!mscl.run(srcfds, dstfds))
        return reporter.fail();

    if (ofs) {
        for (unsigned int i = 0; i < 2; i++) {
            if ((dst_buf_len[i]) && (path(dst).size() > 0)) {
                if (!storeFile(dstbuf[i], ofs, dst_imglen[i])) {
                    std::cerr << "failed to write " << dst_imglen[i] << " bytes to " << path(dst)<< std::endl;
                    return reporter.fail();
                }
            }
        }
    }

    return reporter.okay();
}

static int runWithCommandLine(int argc, char *argv[], Usage &usage)
{
    const std::regex img_regex("(\\d+)x(\\d+)/(nv12|nv12m|yuyv)@([\\w./\\[\\]-]+)");
    ImageQuad src{};
    ImageQuad dst{};
    unsigned int transform = 0;

    for (int i = 0; i < argc; i += 2) {
        std::cmatch match;

        if (!strcmp(argv[i], "-i")) {
            if (!std::regex_match(argv[i + 1], match, img_regex)) {
                usage();
                return -1;
            }

            src = std::make_tuple(stoi(match[1].str()), stoi(match[2].str()), getFormat(match[3].str()), match[4].str());
        } else if (!strcmp(argv[i], "-o")) {
            if (!std::regex_match(argv[i + 1], match, img_regex)) {
                usage();
                return -1;
            }

            dst = std::make_tuple(stoi(match[1].str()), stoi(match[2].str()), getFormat(match[3].str()), match[4].str());
        } else if (!strcmp(argv[i], "-t")) {
            if (!strcmp(argv[i + 1], "h")) {
                transform = HAL_TRANSFORM_FLIP_H;
            } else if (!strcmp(argv[i + 1], "v")) {
                transform = HAL_TRANSFORM_FLIP_V;
            } else if (!strcmp(argv[i + 1], "0")) {
                transform = 0;
            } else if (!strcmp(argv[i + 1], "90")) {
                transform = HAL_TRANSFORM_ROT_90;
            } else if (!strcmp(argv[i + 1], "180")) {
                transform = HAL_TRANSFORM_ROT_180;
            } else if (!strcmp(argv[i + 1], "270")) {
                transform = HAL_TRANSFORM_ROT_270;
            } else if (!strcmp(argv[i + 1], "h90")) {
                transform = HAL_TRANSFORM_ROT_90 | HAL_TRANSFORM_FLIP_H;
            } else if (!strcmp(argv[i + 1], "h270")) {
                transform = HAL_TRANSFORM_ROT_270 & ~HAL_TRANSFORM_FLIP_H;
            } else {
                usage();
                return -1;
            }
        } else {
            std::cerr << "invalid option " << argv[i] << std::endl;
            usage();
            return -1;
        }
    }

    GiantMscl mscl;
    if (!mscl)
        return -1;

    return runSingle(mscl, src, dst, transform) ? 0 : -1;
}

static int runWithJson(const char *json)
{
    std::ifstream ifs(json);
    if (!ifs) {
        std::cerr << "failed to open " << json << std::endl;
        return -1;
    }

    Json::Value root;
    ifs >> root;

    unsigned int repeat_count = root.get("repeat", 1).asUInt();

    GiantMscl mscl;
    if (!mscl)
        return -1;

    unsigned int nr_fail = 0;
    unsigned int total_jobs = 0;

    for (unsigned int i = 0; i < repeat_count; i++) {
        unsigned int job_count = 0;
        unsigned int nr_local_fail = 0;

        for (auto &job: root["jobs"]) {
            ImageQuad src{};
            ImageQuad dst{};
            unsigned int transform = 0;
            bool expected_success = true;

            if (!!job["src"]) {
                auto &img = job["src"];
                src = std::make_tuple(img.get(0U, 0).asUInt(),
                                      img.get(1, 0).asUInt(),
                                      getFormat(img.get(2, "yuyv").asString()),
                                      img.get(3, "").asString());
            } else {
                std::cerr << "'src' field in json is mandatory" << std::endl;
                return -1;
            }
            if (!!job["dst"]) {
                auto &img = job["dst"];
                dst = std::make_tuple(img.get(0U, 0).asUInt(),
                                      img.get(1, 0).asUInt(),
                                      getFormat(img.get(2, "yuyv").asString()),
                                      img.get(3, "").asString());
            } else {
                std::cerr << "'dst' field in json is mandatory" << std::endl;
                return -1;
            }

            
            transform = 0;
            switch (job.get("rotation", 0).asUInt()) {
                case 90:  transform = HAL_TRANSFORM_ROT_90; break;
                case 180: transform = HAL_TRANSFORM_ROT_180; break;
                case 270: transform = HAL_TRANSFORM_ROT_270; break;
            }

            if (!!job["flip"]) {
                const auto &flip = job["flip"].asString();
                if (flip == "x")
                    transform |= HAL_TRANSFORM_FLIP_V;
                else if (flip == "y")
                    transform |= HAL_TRANSFORM_FLIP_H;
            }

            expected_success = job.get("expected_success", true).asBool();

            if (!runSingle(mscl, src, dst, transform, expected_success))
                nr_local_fail++;

            job_count++;
        }

        std::cout << "  -- COMPLETED " << job_count << " JOBS! (" << nr_local_fail << " failed) --" << std::endl;
        nr_fail += nr_local_fail;
        total_jobs += job_count;
    }

    std::cout << std::endl;
    std::cout << "COMPLETED " << repeat_count << " REPEATS! (" << nr_fail << "/" << total_jobs << " failed)" << std::endl;

    return 0;
}

int main(int argc, char *argv[])
{
    Usage usage(argv[0]);

    if ((argc % 2) == 0) {
        std::cerr << "invalid options." << std::endl;
        usage();
        return -1;
    }

    for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "-j"))
            return runWithJson(argv[i + 1]);
    }

    return runWithCommandLine(argc - 1, argv + 1, usage);
}
