#include <iostream>
#include <iomanip>

#include "uapi.h"

#ifdef DEBUG

static const char * cmd_names[] = {
    "SRC_CFG",
    "SRC_WH",
    "SRC_SPAN",
    "SRC_YPOS",
    "SRC_CPOS",
    "DST_CFG",
    "DST_WH",
    "DST_SPAN",
    "DST_POS",
    "V_RATIO",
    "H_RATIO",
    "ROT_CFG",
    "YH_IPHASE",
    "YV_IPHASE",
    "CH_IPHASE",
    "CV_IPHASE",
};

static inline void show(unsigned int idx, mscl_task &task, const char *postfix)
{
    std::cout << std::setw(9) << std::setfill(' ') << std::left << cmd_names[idx] << '=' << std::right << std::setw(8) << std::setfill('0') << task.cmd[idx] << postfix;
}

bool showJob(mscl_job *job)
{
    //std::cout << "Running a MSCL Job..." << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "    .version=" << job->version << ", .taskcount=" << job->taskcount << std::endl;

    for (unsigned int i = 0; i < job->taskcount; i++) {
        std::cout << "    .tasks[" << i << "] = {" << std::endl;

        std::cout << std::hex;
        std::cout << "        .cmd = [";
        show(MSCL_SRC_CFG, job->tasks[i], ", ");
        show(MSCL_SRC_WH, job->tasks[i], ", ");
        show(MSCL_SRC_SPAN, job->tasks[i], ", ");
        show(MSCL_SRC_YPOS, job->tasks[i], ", ");
        show(MSCL_SRC_CPOS, job->tasks[i], " ");
        std::cout << std::endl;
        std::cout << "                ";
        show(MSCL_DST_CFG,  job->tasks[i], ", ");
        show(MSCL_DST_WH,   job->tasks[i], ", ");
        show(MSCL_DST_SPAN, job->tasks[i], ", ");
        show(MSCL_DST_POS,  job->tasks[i], "");
        std::cout << std::endl;
        std::cout << "                ";
        show(MSCL_V_RATIO, job->tasks[i], ", ");
        show(MSCL_H_RATIO, job->tasks[i], ", ");
        show(MSCL_ROT_CFG, job->tasks[i], "");
        std::cout << std::endl;
        std::cout << "                ";
        show(MSCL_SRC_YH_IPHASE, job->tasks[i], ", ");
        show(MSCL_SRC_YV_IPHASE, job->tasks[i], ", ");
        show(MSCL_SRC_CH_IPHASE, job->tasks[i], ", ");
        show(MSCL_SRC_CV_IPHASE, job->tasks[i], "");

        std::cout << "]" << std::endl;
        std::cout << std::dec;

        std::cout << "        .buf[SRC] = {.count=" << job->tasks[i].buf[MSCL_SRC].count
                  << ", .reserved=" << job->tasks[i].buf[MSCL_SRC].reserved;
        std::cout << ", .dmabuf=[";
        std::cout << job->tasks[i].buf[MSCL_SRC].dmabuf[0] << ", "
                  << job->tasks[i].buf[MSCL_SRC].dmabuf[1] << ", "
                  << job->tasks[i].buf[MSCL_SRC].dmabuf[2] << "]";
        std::cout << ", .offset=[";
        std::cout << job->tasks[i].buf[MSCL_SRC].offset[0] << ", "
                  << job->tasks[i].buf[MSCL_SRC].offset[1] << ", "
                  << job->tasks[i].buf[MSCL_SRC].offset[2] << ']';
        std::cout << "}" << std::endl;

        std::cout << "        .buf[DST] = {.count=" << job->tasks[i].buf[MSCL_DST].count
                  << ", .reserved=" << job->tasks[i].buf[MSCL_DST].reserved;
        std::cout << ", .dmabuf=[";
        std::cout << job->tasks[i].buf[MSCL_DST].dmabuf[0] << ", "
                  << job->tasks[i].buf[MSCL_DST].dmabuf[1] << ", "
                  << job->tasks[i].buf[MSCL_DST].dmabuf[2] << "]";
        std::cout << ", .offset=[";
        std::cout << job->tasks[i].buf[MSCL_DST].offset[0] << ", "
                  << job->tasks[i].buf[MSCL_DST].offset[1] << ", "
                  << job->tasks[i].buf[MSCL_DST].offset[2] << ']';
        std::cout << "}" << std::endl;

        std::cout << "    } //tasks[" << i << "]" << std::endl;
    }
    std::cout << "}" << std::endl;

    return true;
}

#endif //DEBUG
