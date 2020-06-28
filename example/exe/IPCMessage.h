// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define IPC_MESSAGE_START TestMsgStart

IPC_SYNC_MESSAGE_CONTROL0_0(SyncChannelTestMsg)

IPC_SYNC_MESSAGE_CONTROL0_1(SyncChannelTestMsg_AnswerToLife, int)
