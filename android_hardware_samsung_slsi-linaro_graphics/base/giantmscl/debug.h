#ifndef _DEBUG_H_
#define _DEBUG_H_

struct mscl_job;
#ifdef DEBUG
bool showJob(mscl_job *job);
#else
static inline bool showJob(mscl_job * __unused job) { return true; }
#endif

#endif //_DEBUG_H_
