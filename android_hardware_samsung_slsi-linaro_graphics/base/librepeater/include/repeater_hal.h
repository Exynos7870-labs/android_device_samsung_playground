/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef REPEATER_H
#define REPEATER_H

void *repeater_open();
void repeater_close(void *handle);

int repeater_map(void *handle, int w, int h, int fps, bool enable_hdcp);
void repeater_unmap(void *handle);

int repeater_start(void *handle);
int repeater_stop(void *handle);
int repeater_pause(void *handle);
int repeater_resume(void *handle);

int repeater_dump(void *handle, char *name);

#endif
