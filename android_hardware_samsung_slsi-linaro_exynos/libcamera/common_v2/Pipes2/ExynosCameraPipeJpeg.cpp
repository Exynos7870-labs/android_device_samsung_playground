/*
**
** Copyright 2013, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/*#define LOG_NDEBUG 0 */
#define LOG_TAG "ExynosCameraPipeJpeg"
#include <cutils/log.h>

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "ExynosCameraPipeJpeg.h"

/* For test */
#include "ExynosCameraBuffer.h"

namespace android {

ExynosCameraPipeJpeg::~ExynosCameraPipeJpeg()
{
    this->destroy();
}

status_t ExynosCameraPipeJpeg::create(__unused int32_t *sensorIds)
{
    m_mainThread = ExynosCameraThreadFactory::createThread(this, &ExynosCameraPipeJpeg::m_mainThreadFunc, "JpegThread");

    m_inputFrameQ = new frame_queue_t(m_mainThread);

    CLOGI("INFO(%s[%d]):create() is succeed (%d)", __FUNCTION__, __LINE__, getPipeId());

    return NO_ERROR;
}

status_t ExynosCameraPipeJpeg::destroy(void)
{
    if (m_inputFrameQ != NULL) {
        m_inputFrameQ->release();
        delete m_inputFrameQ;
        m_inputFrameQ = NULL;
    }

    if (m_shot_ext != NULL) {
        delete m_shot_ext;
        m_shot_ext = NULL;
    }

    CLOGI("INFO(%s[%d]):destroy() is succeed (%d)", __FUNCTION__, __LINE__, getPipeId());

    return NO_ERROR;
}

status_t ExynosCameraPipeJpeg::start(void)
{
    CLOGD("DEBUG(%s[%d])", __FUNCTION__, __LINE__);
    /* TODO: check state ready for start */

    return NO_ERROR;
}

status_t ExynosCameraPipeJpeg::stop(void)
{
    CLOGD("DEBUG(%s[%d])", __FUNCTION__, __LINE__);
    int ret = 0;

    m_mainThread->requestExitAndWait();

    CLOGD("DEBUG(%s[%d]): thead exited", __FUNCTION__, __LINE__);

    m_inputFrameQ->release();

    return NO_ERROR;
}

status_t ExynosCameraPipeJpeg::startThread(void)
{
    CLOGD("DEBUG(%s[%d])", __FUNCTION__, __LINE__);

    start();

    if (m_outputFrameQ == NULL) {
        CLOGE("ERR(%s):outputFrameQ is NULL, cannot start", __FUNCTION__);
        return INVALID_OPERATION;
    }

    m_mainThread->run(m_name);

    CLOGI("INFO(%s[%d]):startThread is succeed (%d)", __FUNCTION__, __LINE__, getPipeId());

    return NO_ERROR;
}

/*
 * The picture that gets saved is mirrored here, for the front camera, on its
 * way into the encoder.  The preview of the front camera is shown mirrored,
 * like a mirror, but the pictures that are saved are not: a selfie of a sign, a
 * book cover or any other text is readable in the file and unreadable on the
 * screen the user framed it with, so the result looks like a picture of the
 * rear camera instead of like the selfie that was taken.
 *
 * The scaler of this platform cannot be asked for the flip: the still capture
 * runs on the SC/MSC node (PICTURE_GSC_NODE_NUM is 4, which is CSC_HW_SC0), so
 * libcsc hands the flip over to libexynosscaler's exynos_sc_set_rotation(),
 * which is closed and drops it.  Everything between that scaler and the file
 * is ours, so the picture is turned around by hand instead - right before it is
 * handed to ExynosJpegEncoderForCamera, which also makes the thumbnail that is
 * embedded in the file carry the same orientation, and covers every JPEG this
 * HAL writes for the front camera: the still capture of the Camera2 API and of
 * the legacy Camera1 API alike, and the reprocessing paths as well.
 *
 * Which way it has to be turned around follows from the orientation that ends
 * up in the EXIF tag of the very same picture: that tag tells the viewer how it
 * rotates the picture for the screen, and the user compares what he sees on the
 * screen with what he sees in the preview, not with the bytes in the file.  A
 * picture the viewer turns by 90 or 270 degrees has to be mirrored top to
 * bottom to look mirrored on the screen, one that is turned by 0 or 180 degrees
 * has to be mirrored left to right.  Doing it the other way around gives a
 * selfie in portrait that is not mirrored but upside down, which is worse than
 * what we started with.
 */

/*
 * Turn the order of the lines of a plane around (top to bottom), which mirrors
 * the picture for a viewer that turns it by 90 or 270 degrees.
 */
static void m_mirrorPlaneLines(unsigned char *base, int stride, int height)
{
    unsigned char *lineBuffer = NULL;
    unsigned char *top = NULL;
    unsigned char *bottom = NULL;
    int y = 0;

    if (base == NULL || stride <= 0 || height <= 1)
        return;

    lineBuffer = (unsigned char *)malloc(stride);
    if (lineBuffer == NULL) {
        ALOGE("ERR(%s[%d]): malloc(%d) fail", __FUNCTION__, __LINE__, stride);
        return;
    }

    /* the line in the middle of an odd height stays where it is */
    for (y = 0; y < (height >> 1); y++) {
        top    = base + ((size_t)y * stride);
        bottom = base + ((size_t)(height - 1 - y) * stride);

        memcpy(lineBuffer, top, stride);
        memcpy(top, bottom, stride);
        memcpy(bottom, lineBuffer, stride);
    }

    free(lineBuffer);
}

/*
 * Turn the groups of unitBytes of one line around, in place.  This is the plain
 * mirror of a plane whose pixels are whole groups: it is what the luma and the
 * interleaved chroma of NV12 and NV21 need.
 */
static void m_mirrorPlaneGroups(unsigned char *row, int stride, int unitBytes)
{
    unsigned char *left = NULL;
    unsigned char *right = NULL;
    unsigned char temp = 0;
    int x = 0;
    int i = 0;

    if (row == NULL || stride <= 0 || unitBytes <= 0)
        return;

    for (x = 0; x + unitBytes <= stride - unitBytes - x; x += unitBytes) {
        left  = row + x;
        right = row + stride - unitBytes - x;

        for (i = 0; i < unitBytes; i++) {
            temp     = left[i];
            left[i]  = right[i];
            right[i] = temp;
        }
    }
}

/*
 * Turn the pixels of one line of packed YUYV around (left to right), which
 * mirrors the picture for a viewer that turns it by 0 or 180 degrees.
 *
 * The bytes of a line are Y U Y V for every two pixels: the two pixels of such
 * a group share both chroma bytes, so they cannot be moved one by one - that
 * would hand a pixel the chroma, or the luma, of its neighbour and tint the
 * picture.  A group travels as a whole to the place of the mirrored group, and
 * inside it the two luma bytes change places, because the pixel that was on the
 * right is the one that is on the left after the mirror.  The group in the
 * middle of an odd number of groups stays where it is and only swaps its two
 * luma bytes with itself.
 */
static void m_mirrorYuyvLine(unsigned char *row, int width)
{
    unsigned char *left = NULL;
    unsigned char *right = NULL;
    unsigned char temp = 0;
    int group = 0;
    int groups = width / 2;

    if (row == NULL || groups <= 0)
        return;

    for (group = 0; group < groups / 2; group++) {
        left  = row + 4 * group;
        right = row + 4 * (groups - 1 - group);

        temp = left[0];  left[0]  = right[2]; right[2] = temp;   /* Y of the right pixel first */
        temp = left[2];  left[2]  = right[0]; right[0] = temp;   /* and the left pixel after it */
        temp = left[1];  left[1]  = right[1]; right[1] = temp;   /* U stays U */
        temp = left[3];  left[3]  = right[3]; right[3] = temp;   /* V stays V */
    }

    /* one group in the middle: mirror its two pixels inside the group */
    if (groups & 1) {
        left = row + 4 * (groups / 2);
        temp = left[0]; left[0] = left[2]; left[2] = temp;
    }
}

/*
 * Turn every line of a plane around inside itself (left to right), which
 * mirrors the picture for a viewer that turns it by 0 or 180 degrees.
 * unitBytes is the size of the smallest group the format defines: one byte for
 * the luma of NV12 and NV21, two for their interleaved chroma, because a chroma
 * sample covers a 2x2 block of luma and has to travel as one.
 */
static void m_mirrorPlanePixels(unsigned char *base, int stride, int height, int unitBytes)
{
    unsigned char *row = NULL;
    int y = 0;

    if (base == NULL || stride <= 0 || height <= 0 || unitBytes <= 0)
        return;

    for (y = 0; y < height; y++) {
        row = base + ((size_t)y * stride);
        m_mirrorPlaneGroups(row, stride, unitBytes);
    }
}

/*
 * Mirror the picture of the front camera in the buffer that goes into the JPEG
 * encoder.  The two formats below are the packed ones the still capture of this
 * platform produces; everything else is left alone and said so in the log,
 * rather than scrambling a picture we do not understand.
 */
static void m_mirrorFrontPicture(ExynosCameraBuffer *buffer, ExynosRect *rect, int orientation)
{
    unsigned char *base = NULL;
    unsigned char *chroma = NULL;
    void *mappedBuffer = NULL;
    size_t neededSize = 0;
    int stride = 0;
    int y = 0;
    int flagVertical = false;

    /* the EXIF orientation is a clockwise turn of 0, 90, 180 or 270 degrees */
    orientation = ((orientation % 360) + 360) % 360;
    flagVertical = (orientation == 90 || orientation == 270);

    switch (rect->colorFormat) {
    case V4L2_PIX_FMT_YUYV:
        /* one packed plane: Y U Y V, two bytes a pixel, chroma shared by both */
        stride = rect->w * 2;
        neededSize = (size_t)stride * rect->h;
        break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
        /* the luma plane, followed by the interleaved chroma of half the height */
        stride = rect->w;
        neededSize = (size_t)stride * rect->h * 3 / 2;
        break;
    default:
        ALOGW("WARN(%s[%d]): picture format(0x%x) is not mirrored",
                __FUNCTION__, __LINE__, rect->colorFormat);
        return;
    }

    if (rect->w <= 0 || rect->h <= 0 || (rect->w & 1) || (rect->h & 1)) {
        ALOGW("WARN(%s[%d]): picture size(%dx%d) is not mirrored",
                __FUNCTION__, __LINE__, rect->w, rect->h);
        return;
    }

    /* rather no mirror than a write past the end of a buffer we misjudged */
    if (buffer->size[0] < neededSize) {
        ALOGW("WARN(%s[%d]): picture buffer(%u) shorter than %u, not mirrored",
                __FUNCTION__, __LINE__, buffer->size[0], (unsigned int)neededSize);
        return;
    }

    base = (unsigned char *)buffer->addr[0];
    if (base == NULL) {
        if (buffer->fd[0] < 0) {
            ALOGE("ERR(%s[%d]): no buffer to mirror", __FUNCTION__, __LINE__);
            return;
        }

        mappedBuffer = mmap(NULL, neededSize, PROT_READ | PROT_WRITE,
                MAP_SHARED, buffer->fd[0], 0);
        if (mappedBuffer == MAP_FAILED) {
            ALOGE("ERR(%s[%d]): mmap(fd %d) fail", __FUNCTION__, __LINE__, buffer->fd[0]);
            return;
        }

        base = (unsigned char *)mappedBuffer;
    }

    if (rect->colorFormat == V4L2_PIX_FMT_YUYV) {
        if (flagVertical == true) {
            m_mirrorPlaneLines(base, stride, rect->h);
        } else {
            for (y = 0; y < rect->h; y++)
                m_mirrorYuyvLine(base + ((size_t)y * stride), rect->w);
        }
    } else {
        chroma = base + ((size_t)stride * rect->h);

        if (flagVertical == true) {
            m_mirrorPlaneLines(base, stride, rect->h);
            m_mirrorPlaneLines(chroma, stride, rect->h / 2);
        } else {
            m_mirrorPlanePixels(base, stride, rect->h, 1);
            m_mirrorPlanePixels(chroma, stride, rect->h / 2, 2);
        }
    }

    if (mappedBuffer != NULL)
        munmap(mappedBuffer, neededSize);

    ALOGD("DEBUG(%s[%d]): front picture %dx%d, orientation %d, mirrored %s",
            __FUNCTION__, __LINE__, rect->w, rect->h, orientation,
            (flagVertical == true) ? "top to bottom" : "left to right");
}

status_t ExynosCameraPipeJpeg::m_run(void)
{
    ExynosCameraAutoTimer autoTimer(__FUNCTION__);
    status_t ret = 0;
    ExynosCameraFrame *newFrame = NULL;

    ExynosCameraBuffer yuvBuf;
    ExynosCameraBuffer jpegBuf;

    ExynosRect pictureRect;
    ExynosRect thumbnailRect;
    int jpegQuality = m_parameters->getJpegQuality();
    int thumbnailQuality = m_parameters->getThumbnailQuality();
    int jpegformat = V4L2_PIX_FMT_JPEG_422;
    {
        jpegformat = (JPEG_INPUT_COLOR_FMT == V4L2_PIX_FMT_YUYV) ?  V4L2_PIX_FMT_JPEG_422 : V4L2_PIX_FMT_JPEG_420;
    }

    memset(m_shot_ext, 0x00, sizeof(struct camera2_shot_ext));

    exif_attribute_t exifInfo;
    m_parameters->getFixedExifInfo(&exifInfo);

    pictureRect.colorFormat = m_parameters->getHwPictureFormat();

    switch (pictureRect.colorFormat) {
    case V4L2_PIX_FMT_NV21:
        jpegformat = V4L2_PIX_FMT_JPEG_420;
        break;
    case V4L2_PIX_FMT_YUYV:
        jpegformat = V4L2_PIX_FMT_JPEG_422;
        break;
    default:
        CLOGE("ERR(%s[%d]):invalid jpeg format pictureRect.colorFormat(%d)", __FUNCTION__, __LINE__, pictureRect.colorFormat);
        break;
    }

    m_parameters->getPictureSize(&pictureRect.w, &pictureRect.h);
    m_parameters->getThumbnailSize(&thumbnailRect.w, &thumbnailRect.h);

    CLOGD("DEBUG(%s[%d]):picture size(%dx%d), thumbnail size(%dx%d)",
            __FUNCTION__, __LINE__, pictureRect.w, pictureRect.h, thumbnailRect.w, thumbnailRect.h);

    ALOGD("DEBUG(%s[%d]):wait JPEG pipe inputFrameQ", __FUNCTION__, __LINE__);
    ret = m_inputFrameQ->waitAndPopProcessQ(&newFrame);
    if (ret < 0) {
        /* TODO: We need to make timeout duration depends on FPS */
        if (ret == TIMED_OUT) {
            CLOGW("WARN(%s):wait timeout", __FUNCTION__);
        } else {
            CLOGE("ERR(%s):wait and pop fail, ret(%d)", __FUNCTION__, ret);
            /* TODO: doing exception handling */
        }
        return ret;
    }

    if (newFrame == NULL) {
        CLOGE("ERR(%s):new frame is NULL", __FUNCTION__);
        return NO_ERROR;
    }

    ALOGD("DEBUG(%s[%d]):JPEG pipe inputFrameQ output done", __FUNCTION__, __LINE__);

    if (m_parameters->getHalVersion() == IS_HAL_VER_3_2) {
        newFrame->getMetaData(m_shot_ext);

        /* JPEG Quality, Thumbnail Quality Setting */
        jpegQuality = (int) m_shot_ext->shot.ctl.jpeg.quality;
        thumbnailQuality = (int) m_shot_ext->shot.ctl.jpeg.thumbnailQuality;

        /* JPEG Thumbnail Size Setting */
        thumbnailRect.w = m_shot_ext->shot.ctl.jpeg.thumbnailSize[0];
        thumbnailRect.h = m_shot_ext->shot.ctl.jpeg.thumbnailSize[1];
    }
    ret = newFrame->getSrcBuffer(getPipeId(), &yuvBuf);
    if (ret < 0) {
        CLOGE("ERR(%s[%d]):frame get src buffer fail, ret(%d)", __FUNCTION__, __LINE__, ret);
        /* TODO: doing exception handling */
        return OK;
    }

    ret = newFrame->getDstBuffer(getPipeId(), &jpegBuf);
    if (ret < 0) {
        CLOGE("ERR(%s[%d]):frame get dst buffer fail, ret(%d)", __FUNCTION__, __LINE__, ret);
        /* TODO: doing exception handling */
        return OK;
    }

    if (m_jpegEnc.create()) {
        CLOGE("ERR(%s):m_jpegEnc.create() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    m_jpegEnc.setExtScalerNum(m_parameters->getScalerNodeNumPicture());

    {
        if (m_jpegEnc.setQuality(jpegQuality)) {
            CLOGE("ERR(%s[%d]):m_jpegEnc.setQuality() fail", __FUNCTION__, __LINE__);
            ret = INVALID_OPERATION;
            goto jpeg_encode_done;
        }
    }

    if (m_jpegEnc.setSize(pictureRect.w, pictureRect.h)) {
        CLOGE("ERR(%s):m_jpegEnc.setSize() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    if (m_jpegEnc.setColorFormat(pictureRect.colorFormat)) {
        CLOGE("ERR(%s):m_jpegEnc.setColorFormat() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    if (m_jpegEnc.setJpegFormat(jpegformat)) {
        CLOGE("ERR(%s):m_jpegEnc.setJpegFormat() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    if (thumbnailRect.w != 0 && thumbnailRect.h != 0) {
        exifInfo.enableThumb = true;
        if (pictureRect.w < 320 || pictureRect.h < 240) {
            thumbnailRect.w = 160;
            thumbnailRect.h = 120;
        }
        if (m_jpegEnc.setThumbnailSize(thumbnailRect.w, thumbnailRect.h)) {
            CLOGE("ERR(%s):m_jpegEnc.setThumbnailSize(%d, %d) fail", __FUNCTION__, thumbnailRect.w, thumbnailRect.h);
            ret = INVALID_OPERATION;
            goto jpeg_encode_done;
        }
        if (0 < thumbnailQuality && thumbnailQuality <= 100) {
            if (m_jpegEnc.setThumbnailQuality(thumbnailQuality)) {
                ret = INVALID_OPERATION;
                CLOGE("ERR(%s):m_jpegEnc.setThumbnailQuality(%d) fail", __FUNCTION__, thumbnailQuality);
            }
        }
    } else {
        exifInfo.enableThumb = false;
    }

    /* wait for medata update */
    if(newFrame->getMetaDataEnable() == false) {
        CLOGD("DEBUG(%s[%d]): Waiting for update jpeg metadata failed (%d) ", __FUNCTION__, __LINE__, ret);
    }

    /* get dynamic meters for make exif info */
    newFrame->getDynamicMeta(m_shot_ext);
    newFrame->getUserDynamicMeta(m_shot_ext);

    m_parameters->setExifChangedAttribute(&exifInfo, &pictureRect, &thumbnailRect, &m_shot_ext->shot);

    /* The HAL puts the flip on the pipe that really produces the picture: the
     * GSC/scaler when the still runs through it (see
     * ExynosCamera3::m_handleIsChainDone()), this pipe when it does not. So
     * there is exactly one flip on the way to the picture. The axis follows
     * the orientation of the picture, the same value the EXIF header gets.
     */
    if (getCameraId() == CAMERA_ID_FRONT
            && (newFrame->getFlipHorizontal(getPipeId()) != 0
                || newFrame->getFlipVertical(getPipeId()) != 0))
        m_mirrorFrontPicture(&yuvBuf, &pictureRect,
                (int)m_shot_ext->shot.ctl.jpeg.orientation);

    if (m_jpegEnc.setInBuf((int *)&(yuvBuf.fd), (int *)yuvBuf.size)) {
        CLOGE("ERR(%s):m_jpegEnc.setInBuf() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    if (m_jpegEnc.setOutBuf(jpegBuf.fd[0], jpegBuf.size[0] + jpegBuf.size[1] + jpegBuf.size[2])) {
        CLOGE("ERR(%s):m_jpegEnc.setOutBuf() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    if (m_jpegEnc.updateConfig()) {
        CLOGE("ERR(%s):m_jpegEnc.updateConfig() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    if (m_jpegEnc.encode((int *)&jpegBuf.size, &exifInfo, (char **)jpegBuf.addr, m_parameters->getDebugAttribute()
                )) {
        CLOGE("ERR(%s):m_jpegEnc.encode() fail", __FUNCTION__);
        ret = INVALID_OPERATION;
        goto jpeg_encode_done;
    }

    newFrame->setJpegSize(jpegBuf.size[0]);

    ret = newFrame->setEntityState(getPipeId(), ENTITY_STATE_FRAME_DONE);
    if (ret < 0) {
        CLOGE("ERR(%s[%d]):set entity state fail, ret(%d)", __FUNCTION__, __LINE__, ret);
        /* TODO: doing exception handling */
        return OK;
    }

    m_outputFrameQ->pushProcessQ(&newFrame);

jpeg_encode_done:
    if (ret != NO_ERROR) {
        CLOGD("[jpegBuf.fd[0] %d][jpegBuf.size[0] + jpegBuf.size[1] + jpegBuf.size[2] %d]",
            jpegBuf.fd[0], jpegBuf.size[0] + jpegBuf.size[1] + jpegBuf.size[2]);
        CLOGD("[pictureW %d][pictureH %d][pictureFormat %d]",
            pictureRect.w, pictureRect.h, pictureRect.colorFormat);
    }

    if (m_jpegEnc.flagCreate() == true)
        m_jpegEnc.destroy();

    CLOGI("DEBUG(%s[%d]): -OUT-", __FUNCTION__, __LINE__);

    return ret;
}

bool ExynosCameraPipeJpeg::m_mainThreadFunc(void)
{
    int ret = 0;

    ret = m_run();
    if (ret < 0) {
        if (ret == TIMED_OUT)
            return true;
        CLOGE("ERR(%s):m_run fail", __FUNCTION__);
        /* TODO: doing exception handling */
        return false;
    }

    /* one time */
    return m_checkThreadLoop();
}

void ExynosCameraPipeJpeg::m_init(void)
{
    m_reprocessing = 1;
    m_csc = NULL;
    m_shot_ext = new struct camera2_shot_ext;
}

}; /* namespace android */
