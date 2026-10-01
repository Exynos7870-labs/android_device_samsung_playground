/*
 * Copyright (c) 2013-2015 TRUSTONIC LIMITED
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the TRUSTONIC LIMITED nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#define LOG_TAG "McClientCommon"
#include "log.h"
#include "mcVersionHelper.h"
#include "mc_linux.h"
#include "client_driver.h"
#include "client_common.h"

struct ClientCommon::Impl {
    pthread_mutex_t mutex;
    int open_count;
    ClientDriver driver;
    ClientInterface* client;
    bool use_proxy_only;
    Impl(): open_count(0), client(nullptr), use_proxy_only(false) {
        pthread_mutex_init(&mutex, nullptr);
    }
};

ClientCommon::ClientCommon(): pimpl_(new Impl) {
}

ClientCommon::~ClientCommon() {
    delete pimpl_;
}

int ClientCommon::open() {
    pthread_mutex_lock(&pimpl_->mutex);
    if (pimpl_->open_count == 0) {
        if (!pimpl_->use_proxy_only && (pimpl_->driver.open() >= 0)) {
            pimpl_->client = &pimpl_->driver;
            LOG_I("Successfully opened driver");
        } else {
            LOG_E("Failed to open lower layers");
        }
    }
    if (pimpl_->client) {
        pimpl_->open_count++;
    }
    pthread_mutex_unlock(&pimpl_->mutex);
    return 0;
}

int ClientCommon::closeCheck() {
    pthread_mutex_lock(&pimpl_->mutex);
    int ret;
    if (pimpl_->open_count > 1) {
        pimpl_->open_count--;
        ret = 0;
    } else {
        errno = EPERM;
        ret = -1;
    }
    pthread_mutex_unlock(&pimpl_->mutex);
    return ret;
}

int ClientCommon::close() {
    pthread_mutex_lock(&pimpl_->mutex);
    int ret = -1;
    /* Not open */
    if (!pimpl_->client) {
        errno = EBADF;
    } else {
        /* Last token */
        if (pimpl_->open_count == 1) {
            ret = pimpl_->client->close();
            pimpl_->client = nullptr;
        } else {
            ret = 0;
        }
        pimpl_->open_count--;
    }
    pthread_mutex_unlock(&pimpl_->mutex);
    return ret;
}

int ClientCommon::freeze() {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->freeze();
}

int ClientCommon::openSession(struct mc_ioctl_open_session& session) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->openSession(session);
}

int ClientCommon::openTrustlet(struct mc_ioctl_open_trustlet& trustlet) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->openTrustlet(trustlet);
}

int ClientCommon::closeSession(uint32_t session_id) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->closeSession(session_id);
}

int ClientCommon::notify(uint32_t session_id) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->notify(session_id);
}

int ClientCommon::waitNotification(const struct mc_ioctl_wait& wait) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->waitNotification(wait);
}

int ClientCommon::malloc(uint8_t **buffer, uint32_t length) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->malloc(buffer, length);
}

int ClientCommon::free(uint8_t *buffer, uint32_t length) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->free(buffer, length);
}

int ClientCommon::map(struct mc_ioctl_map& map) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->map(map);
}

int ClientCommon::unmap(const struct mc_ioctl_map& map) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->unmap(map);
}

int ClientCommon::getError(struct mc_ioctl_geterr& err) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->getError(err);
}

int ClientCommon::getVersion(struct mc_version_info& version_info) {
    if (!pimpl_->client) {
        errno = EBADF;
        return -1;
    }
    return pimpl_->client->getVersion(version_info);
}

void ClientCommon::useProxyOnly(bool yes) {
    pimpl_->use_proxy_only = yes;
}
